# Initial adaptive app pilots

The 1M Settings diagnostic proves actual generated execution only; it has no
checkpoint. Settings/Contacts/Notes 100M prefixes each match their first frozen
checkpoint. This initial collector runs out of 2,048 attempts and leaves most
execution in fallback. Timings are not an ABBA speed trial.

[The indexed full replays](../adaptive-app-replay/README.md) supersede these
prefixes for complete workload correctness. Exact initial runtime sources, raw
reports and observer checks are retained here.
