set pagination off
set print thread-events off
set debuginfod enabled off
set breakpoint pending on
python
import gdb
import os
from pathlib import Path
preview_bytes = 0
preview_reads = 0
# Observe the actual count after the shared GIO read returns. This avoids ABI
# register assumptions and excludes nested reads inside a GVfs implementation.
source = Path(os.environ['MILLER_TEST_REPOSITORY']) / 'src/nautilus-miller-preview.c'
lines = source.read_text().splitlines()
line = next(i for i, text in enumerate(lines, 1) if 'if (count > 0) { g_byte_array_append' in text)
class PreviewReads(gdb.Breakpoint):
    def stop(self):
        global preview_bytes, preview_reads
        count = int(gdb.parse_and_eval('count'))
        preview_reads += 1
        if count > 0:
            preview_bytes += count
        return False
PreviewReads('nautilus-miller-preview.c:' + str(line), internal=True)
end
run
python
print('PREVIEW_READ_BYTES=' + str(preview_bytes))
print('PREVIEW_READ_CALLS=' + str(preview_reads))
end
