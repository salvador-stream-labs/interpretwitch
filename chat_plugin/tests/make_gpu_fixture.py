"""Write a small NVIDIA-style cuBLAS archive and catalogue for the GPU pack tests."""
import hashlib
import json
from pathlib import Path
import random
import sys
import zipfile

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
rng = random.Random(12)
# Compressible but non-trivial content, larger than the extractor's buffers.
files = {
    'pkg/bin/cublas64_12.dll': bytes(rng.choice(b'MZcublas\x00\x01') for _ in range(700_000)),
    'pkg/bin/cublasLt64_12.dll': bytes(rng.getrandbits(8) for _ in range(300_000)),
    'pkg/LICENSE': 'Test licence text\n'.encode() * 200,
}
archive = out / 'archive.zip'
with zipfile.ZipFile(archive, 'w') as z:
    z.writestr('pkg/bin/', b'')
    info = zipfile.ZipInfo('pkg/bin/cublas64_12.dll')
    info.compress_type = zipfile.ZIP_DEFLATED
    z.writestr(info, files['pkg/bin/cublas64_12.dll'])
    # ZIP64 local header for one entry.
    info = zipfile.ZipInfo('pkg/bin/cublasLt64_12.dll')
    info.compress_type = zipfile.ZIP_DEFLATED
    with z.open(info, 'w', force_zip64=True) as f:
        f.write(files['pkg/bin/cublasLt64_12.dll'])
    z.writestr(zipfile.ZipInfo('pkg/LICENSE'), files['pkg/LICENSE'])
    # Present in the archive but not in the catalogue: must never be written.
    z.writestr('pkg/bin/nvblas64_12.dll', b'not approved')
data = archive.read_bytes()
names = {'pkg/bin/cublas64_12.dll': 'cublas64_12.dll', 'pkg/bin/cublasLt64_12.dll': 'cublasLt64_12.dll',
         'pkg/LICENSE': 'LICENSE.txt'}
catalogue = {
    'schema': 1,
    'id': 'cublas-test-1',
    'archive': {'url': 'https://developer.download.nvidia.com/compute/cuda/redist/libcublas/test.zip',
                'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()},
    'files': [{'entry': entry, 'filename': name, 'size': len(files[entry]),
               'sha256': hashlib.sha256(files[entry]).hexdigest()} for entry, name in names.items()],
}
(out / 'manifest.json').write_text(json.dumps(catalogue, indent=2))
print(f'GPU pack fixture: {len(data)} bytes, {len(names)} approved files.')
