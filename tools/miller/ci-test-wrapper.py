import os, pathlib, sys
root=pathlib.Path(__file__).resolve().parents[2]
args=sys.argv[1:]
if pathlib.Path(args[0]).name=='test-miller-input-guard':
    os.execvp(args[0],args)
if pathlib.Path(args[0]).name=='localsearch':
    args=[str((root/os.environ.get('MILLER_SUITE_BUILD', 'build-miller')/arg).resolve()) if (root/os.environ.get('MILLER_SUITE_BUILD', 'build-miller')/arg).is_file() else arg for arg in args]
    code='import os,pathlib,sys;os.chdir(pathlib.Path.home());os.environ["TMPDIR"]="/tmp";os.execvp(sys.argv[1],sys.argv[1:])'
    args=[sys.executable,'-c',code,*args]
os.execv(sys.executable,[sys.executable,str(root/'tools/miller/with-test-home.py'),'--',*args])
