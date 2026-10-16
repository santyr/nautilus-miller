# Performance and resource acceptance

Status: unmeasured for the expanded goal. No performance pass is claimed.

Required fixtures: empty/small, 10,000 entries, 100,000 entries, 30-level chain, long Unicode names, symlink cycle, permission denial, delayed enumeration and rapid replacements. Compare upstream `87bba3c` and candidate with matching build type, host, warm/cold cache policy and services. Record fixture/hardware details, first useful rows, p95 input response, CPU, peak/settled RSS, descriptor and live model/monitor counts.

Target: ordinary local visible feedback around 100 ms p95; remote I/O completion has no arbitrary local deadline. Branch/tab lifecycle loops should settle near a documented bounded-cache baseline, not grow linearly. Profile every loaded ancestor before deciding whether model virtualization is needed. Screenshots and row virtualization alone do not prove these conditions.
