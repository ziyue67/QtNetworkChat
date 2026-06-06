# E2E Hardening Status

QtNetworkChat treats the server as a transparent carrier for private-message E2E envelopes, identity announcements, and rotation control messages. Trust decisions remain local to the client.

## Completed Boundaries

- Authenticated identity-bound agreement using signed request/response transcripts.
- Persistent local E2E identity material, observed peer fingerprints, trust pins, mismatch handling, and cross-device short-code verification.
- Default trust gates for private text and private files.
- History metadata migration for `plaintext`, `encrypted`, and `decrypt-failed` states.
- Private file payload encryption before chunk upload, including Redis/object routing evidence that avoids Redis plaintext or session keys.
- Encrypted private file interruption policy that requires resend rather than automatic old-envelope resume.
- Production crypto backend contract, adapter registry, provider dispatch evidence, operation matrix, harness, execution plan, invocation contract, slot registry, dispatch binding table, callable manifest, sanitized execution result contract, provider C ABI header, provider table ABI/build-probe evidence, provider table binding probe, runtime provider table registration gate, provider operation preflight gate, provider call frame gate, provider invocation dry-run gate, provider invocation result capture gate, provider execution decision gate, provider callback harness gate, provider vector self-test gate, provider execution slot binding gate, provider execution path gate, provider invocation sandbox gate, provider invocation vector result gate, provider invocation execution gate, explicit invocation execution probe with per-operation vector contract schema/input-output contract hashes/fixture classes/expected status, failure, material-policy classes, structural validator, production rotation dry-run/execute evidence, production acceptance gate, and production-required fail-closed behavior.

## Remaining Production Crypto Work

- Replace draft Qt/HMAC primitives with reviewed production operations.
- Make `productionAcceptance.accepted=true` only after every operation has reviewed implementation state, vector evidence, compatibility status, stable probe vector contract evidence, no sensitive material export, passing harness, ready execution plan, callable invocation, reviewed slot, callable dispatch binding, callable manifest ABI/fixture evidence, sanitized execution result evidence, a matching provider C ABI header, passing provider table structural validation, passing provider table registration, passing provider operation preflight, passing provider call frame construction, passing provider invocation dry-run, passing provider invocation result capture, passing provider execution decision, armed provider callback harness, passing provider vector self-test, bindable provider execution slot, mapped provider execution path, ready provider invocation sandbox, captured provider invocation vector results, ready provider invocation execution, passing provider table binding probe, bound provider table symbols, and passing production-required dispatch.
- Replace fail-closed production rotation evidence with real reviewed key/signature/session rotation after the backend is ready and every sanitized operation execution result has passed.
- Extend encrypted file recovery beyond resend-only after the production backend and key rotation model are stable.
