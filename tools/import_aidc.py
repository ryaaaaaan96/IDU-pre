#!/usr/bin/env python3
"""Import the user's visible AIDC tables, without building/linking its closed library.

Only integer enum/define expressions are evaluated (AST allowlist). Identical
rows are deduplicated; conflicting IDs/mappings abort. Original source attribution
and hashes are retained. Runtime firmware never reads the reference repository.
"""
from pathlib import Path
import argparse, ast, collections, hashlib, json, operator, re


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


def generate(root, output):
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
    descriptor=[]; handler_names=set(); cfg=[]; polls=[]; type_mismatches=[]
    writable={m['id'] for m in mapping if m['function']==3}
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
            callback=row['pf_sig_proc']; has_handler=callback not in ('NULL','MPP_NULL_PTR')
            if has_handler:handler_names.add(callback)
            # Local point properties are retained; original table flags do not
            # declare access rights. External writes are limited by FC03 maps.
            parameter=table_name=='g_arm_data_table' and (flags&1 or sid in writable)
            param_flags=['DP_PARAM_WRITE'] if parameter else []
            if flags&1:param_flags+=['DP_PARAM_PERSIST']
            if parameter and val(rng.get('is_recover','0'))>1:param_flags+=['DP_PARAM_RESTORE']
            if parameter and flags&1 and not flags&(4|16):param_flags+=['DP_PARAM_DEFER_STORE']
            for dev in device_ids:
                source='DP_SOURCE_PARAMETER' if parameter else 'DP_SOURCE_LOCAL' if dev==247 else 'DP_SOURCE_MODBUS'
                cfg.append(f'    {{.key={{{dev},{sid}}},.source={source},.type={typ},.width={width},.minimum={lo}LL,.maximum={hi}LL,.initial.words={{{",".join(map(str,initial))}}}'+
                           (f',.param_flags={" | ".join(param_flags)}' if parameter else '')+
                           (',.handler=idu_aidc_process' if has_handler else '')+
                           (',.read_callback=true' if flags&8 else '')+'},')
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
            cfg.append(f'    {{.key={{247,{m["id"]}}},.source=DP_SOURCE_LOCAL,.type=DP_WORDS,.width={m["count"]}}}, /* missing original process definition: unavailable until explicitly supplied */')
    output.mkdir(parents=True,exist_ok=True)
    notice='/* Generated from user-provided AIDC source tables. Original tables: SUNGROW.\n * Regenerate with tools/import_aidc.py; no closed library is linked. */\n'
    (output/'points.inc').write_text(notice+'\n'.join(cfg)+'\n')
    (output/'transmit.inc').write_text(notice+'\n'.join(f'    {{.function={m["function"]},.address={m["address"]},.count={m["count"]},.key={{{m["device"]},{m["id"]}}},.order=DP_LOW_WORD_FIRST}},' for m in mapping)+'\n')
    (output/'models.inc').write_text(notice+'\n'.join(f'    {{.sig={r["sig"]},.model={r["model"]},.offset={r["offset"]},.width={r["width"]},.legacy_cycle={r["original_cycle"]},.initial.words={{{",".join(map(str,r["initial"]))}}}}},' for r in polls)+'\n')
    (output/'handlers.inc').write_text(notice+'\n'.join(f'    {{.sig={r["id"]},.name="{r["sig_id"]}",.callback="{r["pf_sig_proc"]}"}},' for r in descriptor if r['device']==247)+'\n')
    (output/'fields.inc').write_text(notice+'\n'.join(f'static const struct co_field aidc_field_{i}[] = {{{{.sig_id={r["sig"]},.width={r["width"]},.order=DP_LOW_WORD_FIRST}}}};' for i,r in enumerate(polls))+'\n')
    (output/'collect.inc').write_text(notice+'\n'.join(f'    {{.dev_id={dev},.model_protocol=true,.model_id={r["model"]},.address={r["offset"]},.count={r["width"]},.period_ms=CONFIG_IDU_AIDC_VIRTUAL_PERIOD_MS,.fields=aidc_field_{i},.field_count=1}},' for dev in range(11,19) for i,r in enumerate(polls))+'\n')
    (output/'devices.inc').write_text(notice+'\n'.join(f'    {{.dev_id={dev},.unit_id={dev-10},.bus_id=1}},' for dev in range(11,19))+'\n')
    required={k:val(k) for k in symbols.expr if k.startswith('LOCAL_SIG_') or k.startswith('E_USART_') or k.startswith('PORT_WORK_MODE_')}
    (output/'symbols.h').write_text(notice+'#pragma once\n'+'\n'.join(f'#define AIDC_{k} ({v})' for k,v in required.items())+'\n')
    metadata={'source_root':str(root),'source_sha256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sources},
              'duplicates_collapsed':duplicates,'map_duplicates_collapsed':map_duplicates,'overlapping_maps':overlaps,
              'process_descriptors':descriptor,'all_upp_fields':all_models,'transmit':mapping,
              'unresolved_mappings':unresolved,'type_width_mismatches':type_mismatches,'point_count':len(cfg),'parameter_count':sum(r['parameter'] for r in descriptor),'model_poll_count':len(polls)*8,
              'scope':'UPP fields retain original model offsets/cycle codes. Virtual backend timing is an explicit test setting, not a decoded legacy scheduler.'}
    (output/'migration.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n')
    print({k:metadata[k] for k in ['point_count','parameter_count','model_poll_count']},'maps',len(mapping),'duplicates',len(duplicates))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True,help='Path to the reference AIDC CODE checkout; only needed to regenerate tables')
    parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'cfg/equip/aidc/generated')
    args=parser.parse_args();generate(args.source,args.output)
