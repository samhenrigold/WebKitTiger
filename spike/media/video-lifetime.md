# Local video lifetime workload

Open `video-lifetime.html` beside the existing `bbb-480p.mp4`, either as a file URL or through the local media server. Stage both files together. The fixture makes no external requests and does not need another media download.

Keep the page foregrounded and leave its controls alone for 18 seconds. It plays muted and loops. Player A repeatedly changes visible size, pauses/resumes, and is detached/reinserted. At 6 seconds it is replaced by B and explicitly unloaded. B repeats those operations; at 11 seconds C replaces it and B is unloaded. C then remains at 640×360 with controls, still playing after the result. Reload for another run.

The title and `[VIDEO-LIFETIME]` console lines report progress. At 18 seconds the title becomes `VIDEO LIFETIME PASS (API only)` only if all scheduled actions completed, each player emitted metadata/playing/timeupdate events, decoded dimensions and readyState were available, currentTime advanced, and playback continued after pause/resume and reinsertion. C must also advance during the final stable observation. Media/play errors, missing evidence, or action/timer delays over one second produce FAIL. Inspect `window.videoLifetimeResult` for structured results. A crash or a hung script can leave the title at RUN; that is not a pass.

This is a workload for comparing the copied-frame and ring paths. A JavaScript PASS establishes only the API checks above. Capture screenshots/video during resizing and after each replacement; look for tearing, stale frames, incorrect bounds, black output and usable native controls. Inspect parent/child process logs and crash reports separately. The fixture cannot prove displayed-frame integrity, memory safety, 30 fps, audio behavior, or absence of a crash after its final report.
