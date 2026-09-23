| case | verdict | detail |
| --- | --- | --- |
| document.cookie shows non-HttpOnly (incl. ct0) | PASS | ok |
| __Host-/__Secure- prefix rules | PASS | ok |
| long value (3000 bytes) | PASS | ok |
| Expires in 2031 and 2039 | PASS | ok |
| fetch(credentials) sends all, incl. HttpOnly | PASS | ok |
| JS cannot overwrite HttpOnly | PASS | ok |
| Set-Cookie on fetch response; Max-Age=0 deletes | PASS | ok |
| Path=/sub sent under /sub only | PASS | ok |
| 302 chain sets cookies on each hop | PASS | ok |
| http: no Secure cookies sent | PASS | ok |
| http: Set-Cookie with Secure refused | INFO | unexpected c_insecure_secure |
| cross-site top level: A's cookies stay on A | PASS | ok |
| third-party iframe cookie (Safari blocks) | INFO | unexpected c_frame |
| cross-site fetch: Lax/Strict withheld | INFO | unexpected c_lax c_plain c_strict |
| cross-site nav back: Strict withheld | INFO | unexpected c_strict |
| back on A after B: session cookies persist | PASS | ok |
| relaunch: persistent survive | PASS | ok |
| relaunch: session cookies gone | PASS | ok |

18/18 not failing
