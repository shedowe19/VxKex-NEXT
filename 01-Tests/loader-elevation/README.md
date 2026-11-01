# Loader elevation regressions

Run `python run-tests.py --compiler <cl-or-cc>` to compile the actual
`VxKexLdr/parentguard.h` against controlled Win7 token API doubles. No real
token, process, security setting or job is opened or modified by these tests.

The guard accepts a matching non-elevated identity/session/integrity context.
Every elevated or High-integrity caller uses ordinary CreateProcess, including
a matching High parent, so the exact current token restrictions, privileges
and job membership remain Windows' own creator context. Missing query rights,
inconsistent token data and mismatched contexts also reject the parent attribute.

Tests cover High-to-Medium demotion, matching High rejection, normal Medium,
SID/session/integrity/elevation mismatch, all token-query failures and incorrect
returned lengths, plus handle cleanup and GetLastError preservation. Source
integration checks require query-limited parent access, guard-before-attribute
ordering, closing/clearing rejected handles and the ordinary startup fallback.
An explicit API allowlist prevents new undoubled calls from escaping the harness.

This validates the production admission guard and its wiring. A real Windows7
fixture is still required to verify actual child token/job/profile behavior.
Matching non-elevated attributes are not a claim of complete token equivalence
(authentication LUID, network credentials and every restriction are separate).
