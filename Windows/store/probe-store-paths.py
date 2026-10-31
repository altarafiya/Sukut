"""Bounded native initialization only. No processing mode is started."""
import ctypes
import json
import os
from pathlib import Path
import sys
import time

project=Path(sys.argv[1])
kit=project/'Store'/'Microsoft-Store-2026-10-20'
library=project/'Windows'/'build'/'sukut-0.2.34-msix'
resource_root=project/'Windows'/'build'/'store-input'
data=kit/'evidence'/'native-path-probe'
if data.exists():
    raise RuntimeError('Use a new probe data folder')
data.mkdir()
(data/'runtime').mkdir()
with os.add_dll_directory(str(library)):
    engine=ctypes.CDLL(str(library/'SukutEngine.dll'))
    engine.Sukut_CreateWithPaths.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p]
    engine.Sukut_CreateWithPaths.restype=ctypes.c_void_p
    engine.Sukut_Read.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int]
    engine.Sukut_Read.restype=ctypes.c_int
    engine.Sukut_Destroy.argtypes=[ctypes.c_void_p]
    engine.Sukut_Destroy.restype=None
    handle=engine.Sukut_CreateWithPaths(str(data),str(resource_root))
    if not handle:
        raise RuntimeError('The split-path native context could not be created')
    try:
        for _ in range(40):
            size=engine.Sukut_Read(handle,None,0)
            if not 0 < size < 4_000_000: raise RuntimeError('Invalid native read response size')
            buffer=ctypes.create_string_buffer(size)
            engine.Sukut_Read(handle,buffer,size)
            state=json.loads(buffer.value)
            if state.get('initialized'): break
            time.sleep(.05)
        assert state.get('initialized'), 'Native controller did not finish initialization'
        assert not state.get('pipelineActive'), 'Unexpected active audio pipeline'
        assert not (data/'runtime'/'sukut-routing.ini').exists(), 'Unexpected live routing journal'
        assert not (resource_root/'runtime').exists(), 'Native code wrote runtime data into resource root'
        result={'context_created_with_separate_paths':True,'controller_initialized':True,
                'audio_pipeline_started':False,'routing_journal_created':False,
                'resource_runtime_directory_created':False,'endpoint_metadata_emitted':False,
                'does_not_validate_installed_msix':True}
    finally:
        engine.Sukut_Destroy(handle)
(kit/'evidence'/'native-path-probe.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result))
