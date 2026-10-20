#!/usr/bin/env python3
"""Import the user's visible AIDC tables, without building/linking its closed library.

Only integer enum/define expressions are evaluated (AST allowlist). Identical
rows are deduplicated; conflicting IDs/mappings abort. Original source attribution
and hashes are retained. Runtime firmware never reads the reference repository.
Generated rows and counts replace marked regions in C tables/headers; text outside those
regions is preserved. All destination markers are checked before any file is written.
"""
from pathlib import Path
import argparse, ast, collections, hashlib, json, operator, re


CONFIG_DIR = Path(__file__).resolve().parents[1] / 'app/cfg'
TABLE_FILES = {
    'points': 'tables/process.c',
    'defaults': 'tables/process.c',
    'transmit': 'tables/transmit.c',
    'fields': 'tables/collect.c',
    'collect': 'tables/collect.c',
    'counts': 'inc/tables.h',
    'devices': 'device.c',
}

REFERENCE_CALLBACKS = {
    'local_equip_take_sig_effect': 'idu_serial_setting',
    'link_inner_sn_value': 'idu_point_alias',
    'device_name_3X_2516': 'idu_point_alias',
    'property_prtol_3X_2500_2502': 'idu_protocol_identity',
    'property_prtol_3X_2569_2575': 'idu_protocol_identity',
    'property_prtol_3X_2581': 'idu_protocol_identity',
    'property_prtol_3X_2582_2657': 'idu_firmware_version',
}


def signal_symbol(row):
    name = row['sig_id']
    return 'AIDC_' + name if re.fullmatch(r'[A-Za-z_]\w*', name) else str(row['id'])


def render_reference_tables(metadata, value):
    """Render the public reference layouts; no native execution descriptors."""
    notice = ('/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.\n'
              ' * Regenerate with tools/import_aidc.py; no closed library is linked. */\n')
    groups = {247: [], 11: []}
    defaults = []
    expected_size = {'DATA_TYPE_U8': 1, 'DATA_TYPE_U16': 2, 'DATA_TYPE_S16': 2,
                     'DATA_TYPE_U32': 4, 'DATA_TYPE_S32': 4}
    union_member = {'DATA_TYPE_U8': 'u16', 'DATA_TYPE_U16': 'u16',
                    'DATA_TYPE_S16': 's16', 'DATA_TYPE_U32': 'u32',
                    'DATA_TYPE_S32': 's32'}
    for row in metadata['process_descriptors']:
        dev, sig, flags = row['device'], signal_symbol(row), row['flags']
        size, dtype = value(row['sig_size']), row['data_type']
        # Preserve the existing explicit normalization of malformed source
        # type/size pairs; they remain opaque words, never truncated integers.
        if dtype in expected_size and size != expected_size[dtype]:
            dtype = 'DATA_TYPE_U16ARRAY'
        callback = (REFERENCE_CALLBACKS.get(row['pf_sig_proc'], 'idu_unsupported_service')
                    if row['pf_sig_proc'] not in ('NULL', 'MPP_NULL_PTR')
                    else 'idu_initialize_point' if dev == 247 and not flags & 1 else 'NULL')
        groups[dev].append(
            f'    {{.sig_id={sig},.sig_size={size},.sig_flag={flags},'
            f'.data_type={dtype},.pf_sig_proc={callback}}},')
        initial = row['initial']
        if size > 4:
            default = '.def_value.pst=(void *)(const uint16_t[]){' + ','.join(map(str, initial)) + '}'
        else:
            default = f'.def_value.u32={sum(n << (16*i) for i,n in enumerate(initial))}U'
        member = union_member.get(dtype)
        bounds = ''
        if member:
            for kind in ('min', 'max'):
                entry = next((v for k,v in row['range'].items()
                              if k.startswith(kind + '_value.') and not k.endswith('.pst')), None)
                if entry is not None:
                    bounds += f',.{kind}_value.{member}={value(entry)}'
        restore = str(value(row['range'].get('is_recover', '0')) > 1).lower()
        defaults.append(f'    {{.dev_id={dev},.sig_id={sig},.data={{{default}{bounds}}},'
                        f'.restore={restore}}},')
    for row in metadata['unresolved_mappings']:
        groups[247].append(
            f'    {{.sig_id={signal_symbol(row)},.sig_size={row["count"]*2},'
            '.data_type=DATA_TYPE_U16ARRAY}, /* Missing source definition: no INIT value. */')
    process = notice
    for dev, name, count in (
            (247, 'idu_imported_local_process', 'IDU_IMPORTED_LOCAL_PROCESS_COUNT'),
            (11, 'idu_imported_dsp_process', 'IDU_IMPORTED_DSP_PROCESS_COUNT')):
        process += f'const DM_SIG_CFG_S {name}[] = {{\n' + '\n'.join(groups[dev]) + '\n};\n'
        process += (f'BUILD_ASSERT(ARRAY_SIZE({name}) == {count},\n'
                    f'             "Update {count} in tables.h");\n')

    # Retain only the selected fields used by the existing model backend.
    # The full original field catalog remains in the provenance report.
    models = collections.OrderedDict()
    for row in metadata['process_descriptors']:
        if row['device'] != 11:
            continue
        field = next(r for r in metadata['all_upp_fields'] if r['id'] == row['id'])
        models.setdefault(field['model'], []).append(field)
    fields = notice
    for model, rows in models.items():
        fields += f'static const UPP_M_MOD_DATA model_{model}[] = {{\n'
        for row in rows:
            rw = {'UPP_MODE_R': 1, 'UPP_MODE_RW': 3}[row['rw_mode']]
            fields += (
                f'    {{.sig_id={signal_symbol(row)},.mod_data_offset={value(row["mod_data_offset"])},'
                f'.mod_data_len={value(row["mod_data_len"])},'
                f'.mod_data_type={row["mod_data_type"].replace("UPP_DATA_TYPE_", "DATA_TYPE_")},'
                f'.data_len={value(row["data_len"])},'
                f'.data_type={row["data_type"].replace("UPP_DATA_TYPE_", "DATA_TYPE_")},'
                f'.rw_mode={rw},.byte_order=0,.set_pri={value(row["set_pri"])},'
                f'.scan_pri={value(row["scan_pri"])},.rqst_cyl={value(row["rqst_cyl"])},'
                f'.dec_p={value(row["dec_p"])}}},\n')
        fields += '};\n'
    collection = notice + 'static const UPP_M_MOD models[] = {\n'
    for model in models:
        collection += (f'    {{.p_data=model_{model},.data_num=ARRAY_SIZE(model_{model}),'
                       f'.mod_id={model}}},\n')
    collection += ('};\nconst UPP_M_MOD_REG idu_imported_models = {\n'
                   '    .p_model_ary=models,.model_num=ARRAY_SIZE(models),.cfg_mode=0,\n};\n')
    collection += ('BUILD_ASSERT(' +
                   ' +\n             '.join(f'ARRAY_SIZE(model_{model})' for model in models) +
                   ' == IDU_IMPORTED_MODEL_FIELD_COUNT,\n'
                   '             "Update IDU_IMPORTED_MODEL_FIELD_COUNT in tables.h");\n')
    return {
        'points': process,
        # Rows belong to the common-country part of process.c's single array.
        'defaults': notice + '\n'.join(defaults) + '\n',
        'fields': fields,
        'collect': collection,
        'counts': (notice + 'enum {\n'
                   f'    IDU_IMPORTED_LOCAL_PROCESS_COUNT = {len(groups[247])},\n'
                   f'    IDU_IMPORTED_DSP_PROCESS_COUNT = {len(groups[11])},\n'
                   f'    IDU_IMPORTED_MODEL_FIELD_COUNT = {sum(map(len, models.values()))},\n'
                   f'    IDU_IMPORTED_TRANSMIT_COUNT = {len(metadata["transmit"])},\n'
                   '};\n'),
        'transmit': notice + '\n'.join(
            f'    {{.sig_id={signal_symbol(r)},.reg_addr={r["address"]},.reg_num={r["count"]},'
            f'.dev_id={r["device"]},.reg_type={r["function"]},.byte_order=BYTE_ORDER_DEFAULT}},'
            for r in metadata['transmit']) + '\n',
        'devices': notice + '\n'.join(
            '    {\n'
            f'        .dev_id = {dev}, .coll_addr = {dev-10}, .port = 1,\n'
            '        .ptl_tbl_cfg[0] = {\n'
            '            .proto_type = PRO_DATA_PROCESS,\n'
            '            .sig_num = IDU_IMPORTED_DSP_PROCESS_COUNT,\n'
            '            .pst_table = idu_imported_dsp_process,\n'
            '        },\n'
            '        .ptl_tbl_cfg[1] = {\n'
            '            .proto_type = PRO_UPP_MASTER,\n'
            '            .sig_num = IDU_IMPORTED_MODEL_FIELD_COUNT,\n'
            '            .pst_table = &idu_imported_models,\n'
            '        },\n'
            '    },'
            for dev in range(11, 19)) + '\n',
    }


def replace_imported_tables(text, tables, filename):
    """Replace unique, disjoint marker pairs without changing surrounding C."""
    replacements = []
    for name, content in tables.items():
        begin = f'/* BEGIN IMPORTED TABLE: {name} */'
        end = f'/* END IMPORTED TABLE: {name} */'
        if text.count(begin) != 1 or text.count(end) != 1:
            raise ValueError(f'{filename}: expected one BEGIN/END marker pair for {name}')
        first = text.index(begin)
        last = text.index(end)
        if last < first:
            raise ValueError(f'{filename}: reversed marker pair for {name}')
        replacements.append((first, first + len(begin), last, last + len(end), content))
    replacements.sort()
    if any(left[3] > right[0] for left, right in zip(replacements, replacements[1:])):
        raise ValueError(f'{filename}: imported table regions overlap')
    for _, first, last, _, content in reversed(replacements):
        text = text[:first] + '\n' + content + text[last:]
    return text


def write_tables(output, tables, symbols, metadata, template_dir=None, report_path=None):
    """Prepare all tables/headers before writing; missing files use repository templates."""
    if set(tables) != set(TABLE_FILES):
        raise ValueError('Incomplete imported table set')
    output = Path(output)
    template_dir = CONFIG_DIR if template_dir is None else Path(template_dir)
    if report_path is None:
        report_path = (CONFIG_DIR.parents[1] / 'docs/point-table-import.json'
                       if output.resolve() == CONFIG_DIR.resolve()
                       else output / 'migration.json')
    report_path = Path(report_path)
    grouped = collections.defaultdict(dict)
    for name, filename in TABLE_FILES.items():
        grouped[filename][name] = tables[name]
    prepared = {}
    for filename, regions in grouped.items():
        target = output / filename
        source = target if target.exists() else template_dir / filename
        # Decode bytes directly so existing CRLF outside the regions is retained.
        original = source.read_bytes().decode('utf-8')
        prepared[target] = replace_imported_tables(original, regions, filename)
    prepared[output / 'inc/imported_signals.h'] = symbols
    if report_path.resolve() in {target.resolve() for target in prepared}:
        raise ValueError(f'Report path conflicts with table output: {report_path}')
    prepared[report_path] = json.dumps(metadata, ensure_ascii=False, indent=2) + '\n'
    for target, content in prepared.items():
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content.encode('utf-8'))


def clean(s):
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',
                  lambda m: re.sub(r'[^\n]', ' ', m[0]) if m[0].startswith('/') else m[0], s, flags=re.S)


def entries(text, table):
    s = clean(text)
    m = re.search(r'\b' + re.escape(table) + r'\s*\[\s*\]\s*=\s*\{', s)
    if not m:
        raise ValueError(table)
    pos, depth, begin, result = m.end(), 1, None, []
    quote, escape = None, False
    while depth:
        c = s[pos]
        if quote:
            if escape: escape = False
            elif c == '\\': escape = True
            elif c == quote: quote = None
        elif c in ('"', "'"): quote = c
        elif c == '{':
            if depth == 1: begin = pos
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 1:
                body = s[begin:pos+1]
                fields = dict(re.findall(r'\.([\w.]+)\s*=\s*([^,{}]+)', body))
                fields = {k: v.strip() for k, v in fields.items()}
                fields['line'] = s[:begin].count('\n') + 1
                result.append(fields)
        pos += 1
    return result


class Symbols:
    def __init__(self): self.expr, self.cache = {}, {}
    def read(self, text):
        s = clean(text)
        for name, value in re.findall(r'^\s*#define\s+(\w+)[ \t]+([^\n]+)', s, re.M):
            if not value.rstrip().endswith('\\'): self.expr.setdefault(name, value.strip())
        for body in re.findall(r'\benum\b[^;{}]*\{([^{}]*)\}', s, re.S):
            previous = None
            for part in body.split(','):
                m = re.fullmatch(r'\s*(\w+)\s*(?:=\s*(.*?))?\s*', part, re.S)
                if not m: continue
                name, value = m.groups()
                self.expr.setdefault(name, value if value is not None else f'({previous}) + 1' if previous else '0')
                previous = name
    def value(self, expression):
        expression = re.sub(r'\b(0[xX][\da-fA-F]+|\d+)[uUlL]+\b', r'\1', expression.strip())
        expression = re.sub(r'\(\s*(?:UINT(?:8|16|32|64)|INT(?:8|16|32|64))\s*\)', '', expression)
        tree = ast.parse(expression, mode='eval').body
        binary = {ast.Add:operator.add, ast.Sub:operator.sub, ast.Mult:operator.mul,
                  ast.BitOr:operator.or_, ast.BitAnd:operator.and_, ast.LShift:operator.lshift,
                  ast.RShift:operator.rshift, ast.Div:operator.floordiv}
        def walk(node):
            if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
                if int(node.value) != node.value: raise ValueError(f"Non-integral integer initializer: {expression}")
                return int(node.value)
            if isinstance(node, ast.Name):
                if node.id not in self.cache: self.cache[node.id] = self.value(self.expr[node.id])
                return self.cache[node.id]
            if isinstance(node, ast.BinOp) and type(node.op) in binary: return binary[type(node.op)](walk(node.left),walk(node.right))
            if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub): return -walk(node.operand)
            raise ValueError(expression)
        return walk(tree)


def generate(root, output, report_path=None):
    source_paths = ['platform/inc/pub_sig_def.h','platform/inc/data_process.h','platform/inc/arc_public.h',
                    'platform/inc/arc_byte_order.h','platform/inc/port.h','platform/inc/local_equip.h',
                    'app/globalfunction.h','app/cfg/inc/signal_id_map.h','app/cfg/inc/dev_cfg.h',
                    'app/cfg/equip/table/process.c','app/cfg/equip/table/collect.c',
                    'app/cfg/equip/table/transmit.c','app/cfg/equip/range/signal_range.c']
    sources = {p:(root/p).read_text() for p in source_paths}
    symbols = Symbols()
    for s in sources.values(): symbols.read(s)
    val = symbols.value
    process = sources['app/cfg/equip/table/process.c']
    collect = sources['app/cfg/equip/table/collect.c']
    ranges = sources['app/cfg/equip/range/signal_range.c']
    tables = {}
    duplicates = []
    for name in ['g_arm_data_table','g_dsp_data_table']:
        table = {}
        for row in entries(process,name):
            sid = val(row['sig_id']); row['id'] = sid
            if sid in table:
                comparable = {k:v for k,v in row.items() if k not in ('line','sig_id')}
                old = {k:v for k,v in table[sid].items() if k not in ('line','sig_id')}
                if comparable != old: raise ValueError(f'Conflicting process ID {sid}: {row}')
                duplicates.append({'table':name,'sig_id':sid,'line':row['line']}); continue
            table[sid] = row
        tables[name] = table
    all_models = []
    for model in [1,2,99,100,102,103]:
        for row in entries(collect,f'g_dsp_upp_collect_{model}'):
            row = dict(row, model=model, id=val(row['sig_id']))
            all_models.append(row)
    mapping = []
    map_duplicates = []
    overlaps = []
    for row in entries(sources['app/cfg/equip/table/transmit.c'],'g_arm_transmit_table_t'):
        mapped = dict(row, id=val(row['sig_id']), address=val(row['reg_addr']),
                      count=val(row['reg_num']), function=val(row['reg_type']), device=val(row['dev_id']))
        same = next((m for m in mapping if m['address']==mapped['address'] and m['function']==mapped['function']),None)
        if same:
            if any(same[k]!=mapped[k] for k in ['id','device','count']): raise ValueError(f'Conflicting map {mapped}')
            map_duplicates.append(mapped); continue
        for m in mapping:
            if m['function']==mapped['function'] and mapped['address']<m['address']+m['count'] and m['address']<mapped['address']+mapped['count']:
                overlaps.append({'first_line':m['line'],'second_line':mapped['line'],'first_address':m['address'],'second_address':mapped['address'],'function':m['function']})
        mapping.append(mapped)
    range_tables = {name:{val(r['sig_id']):r for r in entries(ranges,name)} for name in ['g_local_range_table','g_dsp_range_table']}
    descriptor=[]; polls=[]; type_mismatches=[]
    point_count = 0
    type_map={'DATA_TYPE_U8':('DP_U8',0,255),'DATA_TYPE_U16':('DP_U16',0,65535),
              'DATA_TYPE_S16':('DP_S16',-32768,32767),'DATA_TYPE_U32':('DP_U32',0,4294967295),
              'DATA_TYPE_S32':('DP_S32',-2147483648,2147483647),'DATA_TYPE_STRING':('DP_WORDS',0,0)}
    for table_name, device_ids, range_name in [('g_arm_data_table',[247],'g_local_range_table'),('g_dsp_data_table',list(range(11,19)),'g_dsp_range_table')]:
        for sid,row in tables[table_name].items():
            typ,lo,hi=type_map[row['data_type']]; size=val(row['sig_size']); width=(size+1)//2; flags=val(row['sig_flag'])
            rng=range_tables[range_name].get(sid,{})
            if typ!='DP_WORDS' and size!={'DP_U8':1,'DP_U16':2,'DP_S16':2,'DP_U32':4,'DP_S32':4}[typ]:
                type_mismatches.append({'device':device_ids[0],'id':sid,'declared_type':row['data_type'],'bytes':size,'line':row['line']})
                typ,lo,hi='DP_WORDS',0,0
            initial=[0]*width
            for k,v in rng.items():
                if k.startswith('min_value.') and not k.endswith('pst'):lo=val(v)
                if k.startswith('max_value.') and not k.endswith('pst'):hi=val(v)
                if k.startswith('def_value.'):
                    if k.endswith('pst'):
                        m=re.search(r'"(?:\\.|[^"\\])*"',v)
                        if m:
                            data=ast.literal_eval(m[0]).encode()[:size].ljust(width*2,b'\0')
                            initial=[int.from_bytes(data[i:i+2],'big') for i in range(0,len(data),2)]
                    else:
                        n=val(v);initial=[(n>>(16*i))&65535 for i in range(width)]
            parameter = table_name == 'g_arm_data_table' and bool(flags & 1)
            point_count += len(device_ids)
            descriptor.append(dict(row,device=247 if table_name=='g_arm_data_table' else 11,width=width,
                                   flags=flags,initial=initial,parameter=parameter,range=rng))
            if table_name=='g_dsp_data_table':
                candidates=[r for r in all_models if r['id']==sid]
                if not candidates:raise ValueError(f'DSP point {sid} has no model field')
                r=candidates[0]
                polls.append(dict(sig=sid,model=r['model'],offset=val(r['mod_data_offset']),width=width,
                                  original_cycle=val(r['rqst_cyl']),initial=initial))
    unresolved=[]
    for m in mapping:
        if m['id'] not in tables['g_arm_data_table']:
            unresolved.append(m)
            point_count += 1
    notice = ('/* Imported from user-provided AIDC source tables. Original tables: SUNGROW.\n'
              ' * Regenerate with tools/import_aidc.py; no closed library is linked. */\n')
    required={k:val(k) for k in symbols.expr if k.startswith('LOCAL_SIG_') or k.startswith('E_USART_') or k.startswith('PORT_WORK_MODE_')}
    for row in descriptor + all_models + mapping:
        if re.fullmatch(r'[A-Za-z_]\w*', row['sig_id']):
            required[row['sig_id']] = row['id']
    generated_symbols=notice+'#pragma once\n'+'\n'.join(f'#define AIDC_{k} ({v})' for k,v in required.items())+'\n'
    metadata={'source_root':str(root),'source_sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sources},
              'duplicates_collapsed':duplicates,'map_duplicates_collapsed':map_duplicates,'overlapping_maps':overlaps,
              'process_descriptors':descriptor,'all_upp_fields':all_models,'transmit':mapping,
              'unresolved_mappings':unresolved,'type_width_mismatches':type_mismatches,'point_count':point_count,'parameter_count':sum(r['parameter'] for r in descriptor),'model_poll_count':len(polls)*8,
              'scope':'UPP fields retain original model offsets/cycle codes. Virtual backend timing is an explicit test setting, not a decoded legacy scheduler.'}
    generated = render_reference_tables(metadata, val)
    write_tables(output, generated, generated_symbols, metadata, report_path=report_path)
    print({k:metadata[k] for k in ['point_count','parameter_count','model_poll_count']},'maps',len(mapping),'duplicates',len(duplicates))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True,help='Path to the reference AIDC CODE checkout; only needed to regenerate tables')
    parser.add_argument('--output',type=Path,default=CONFIG_DIR,
                        help='Configuration directory containing marked C tables (default: app/cfg); missing files use repository templates')
    parser.add_argument('--report',type=Path,
                        help='Import report path (default: docs/point-table-import.json for repository output, otherwise OUTPUT/migration.json)')
    args=parser.parse_args();generate(args.source,args.output,args.report)
