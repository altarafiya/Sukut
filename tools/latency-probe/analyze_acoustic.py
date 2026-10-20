"""Compare approved test chirps with microphone PCM, using the phone's monotonic clock."""
import json
import sys
from pathlib import Path
import numpy as np

def analyze(directory):
    directory=Path(directory)
    result=json.loads((directory/'result.json').read_text(encoding='utf-8'))
    if 'error' in result:
        return {'run':directory.name,'error':result['error']}
    rate=result['sample_rate']
    x=np.fromfile(directory/'microphone.pcm',dtype='<i2').astype(np.float64)/32768
    n=result['pulse_frames']; i=np.arange(n); t=i/rate
    reference=.025*np.sin(2*np.pi*(700*t+(3400-700)/(2*(n/rate))*t*t))*np.sin(np.pi*i/n)**2
    size=1 << (len(x)+n-1).bit_length()
    correlation=np.fft.irfft(np.fft.rfft(x,size)*np.conj(np.fft.rfft(reference,size)),size)[:len(x)-n+1]
    energy=np.concatenate(([0.],np.cumsum(x*x)))
    score=np.abs(correlation)/np.sqrt(np.maximum((energy[n:]-energy[:-n])*np.dot(reference,reference),1e-20))
    stamps=np.asarray(result['mic_timestamps'],dtype=np.float64)
    if len(stamps)<20:
        return {'run':directory.name,'error':'Insufficient microphone hardware timestamps'}
    offsets=stamps[:,1]-stamps[:,0]*1e9/rate
    origin=float(np.median(offsets[5:]))
    measurements=[]
    for event in result['events']:
        enqueued=(event['enqueue_before_ns']+event['enqueue_after_ns'])/2
        start=max(0,int((enqueued-origin)*rate/1e9))
        end=min(len(score),start+int(1.8*rate))
        at=start+int(np.argmax(score[start:end]))
        delay=(origin+at*1e9/rate-enqueued)/1e6
        measurements.append({'source_frame':event['frame'],'delay_ms':delay,'correlation':float(score[at]),
            'enqueue_call_ms':(event['enqueue_after_ns']-event['enqueue_before_ns'])/1e6,
            'queued_source_frames':event['queued_frames']})
    valid=[m['delay_ms'] for m in measurements if m['correlation']>=.25]
    summary={'run':directory.name,'definition':'source enqueue -> microphone sample; ON minus matched OFF estimates added audible path delay',
        'valid_chirps':len(valid),'chirps':measurements,'mic_timestamp_origin_spread_ms':float(np.ptp(offsets[5:])/1e6),
        'source_buffer_frames':result['source_buffer_frames'],'source_underruns':result['source_underruns'],
        'source_underruns_while_feeding':result.get('source_underruns_while_feeding'),
        'source_device_type':result['source_device_type'],'mic_device_type':result['mic_device_type']}
    if valid:
        summary.update(median_ms=float(np.median(valid)),min_ms=float(np.min(valid)),max_ms=float(np.max(valid)),p95_ms=float(np.percentile(valid,95)))
    return summary

if __name__=='__main__':
    summaries=[analyze(p) for p in sys.argv[1:]]
    print(json.dumps(summaries,indent=2))
