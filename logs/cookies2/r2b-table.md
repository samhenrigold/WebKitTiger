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
| http: Set-Cookie with Secure refused | PASS | ok |
| SameSite=None without Secure refused | PASS | ok |
| unknown SameSite value kept (Lax by default) | PASS | ok |
| cross-site top level: A's cookies stay on A | PASS | ok |
| 3p iframe: Set-Cookie and document.cookie blocked | PASS | ok |
| 3p iframe: its own-site fetch sends nothing | PASS | ok |
| cross-site subresource: only SameSite=None sent | PASS | ok |
| cross-site top-level POST: Lax withheld, Lax-by-default <2 min sent | PASS | ok |
| cross-site top-level GET: Lax sent, Strict withheld | PASS | ok |
| back on A after B: session cookies persist | PASS | ok |
| 3p iframe, no interaction: B's cookies blocked | PASS | ok |
| relaunch: persistent survive | PASS | ok |
| relaunch: session cookies gone | PASS | ok |
| 3p iframe after user interaction with B: None sent, Lax not | PASS | ok |

24/24 not failing
