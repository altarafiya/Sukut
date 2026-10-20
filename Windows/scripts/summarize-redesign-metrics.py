from pathlib import Path
import csv, json, statistics as s
from datetime import datetime, timedelta
root=Path(__file__).resolve().parents[1]/'docs/redesign/metrics'
report={}
for phase in ['baseline','after']:
    rows=[]
    for p in sorted((root/phase).glob('*-process.json')):
        samples=[v for v in json.loads(p.read_text(encoding='utf-8-sig')) if not v['settle']]
        result={'scenario':p.stem.removesuffix('-process'),'samples':len(samples)}
        for field in ['private_bytes','working_set_bytes','cpu_one_core_percent','ui_cpu_one_core_percent','handles','threads']:
            if field in samples[0]:result[field+'_mean']=s.mean(v[field] for v in samples)
        counters=p.with_name(result['scenario']+'-counters.csv')
        if counters.exists():
            data=list(csv.DictReader(counters.open(encoding='utf-8-sig')))
            stamp=lambda r:datetime.strptime(r['Timestamp'],'%m/%d/%Y %H:%M:%S')
            end_settle=stamp(data[0])+timedelta(seconds=60)
            data=[r for r in data if stamp(r)>=end_settle]
            for generation in [0,1,2]:
                result[f'gen{generation}_collections']=sum(float(r['Mean/Increment']) for r in data if r['Counter Name'].startswith(f'Gen {generation} GC Count'))
            loh=[float(r['Mean/Increment']) for r in data if r['Counter Name'].startswith('LOH Size')]
            result['loh_size_bytes_max']=max(loh,default=None)
        rows.append(result)
    report[phase]=rows
(root/'comparison.json').write_text(json.dumps(report,indent=2))
for phase,scenarios in report.items():
    for row in scenarios:
        print(phase,row['scenario'],'private MiB',round(row['private_bytes_mean']/1048576,2),'CPU one core',round(row['cpu_one_core_percent_mean'],3),'Gen2',row.get('gen2_collections'),'LOH',row.get('loh_size_bytes_max'))
