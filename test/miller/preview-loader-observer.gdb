set pagination off
set print thread-events off
set breakpoint pending on
python
import gdb
preview_loads = 0
class PreviewLoaderCalls(gdb.Breakpoint):
    def stop(self):
        global preview_loads
        frame = gdb.newest_frame()
        for unused in range(8):
            if frame is None:
                break
            if frame.name() == 'image_bytes_ready':
                preview_loads += 1
                break
            frame = frame.older()
        return False
PreviewLoaderCalls('gly_loader_load_async')
end
run
python
print('GLYCIN_PREVIEW_REQUESTS=' + str(preview_loads))
end
