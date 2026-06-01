# End-to-End Encryption Hardening Plan

QtNetworkChat currently treats the server as a transparent carrier for private-message E2E envelopes, identity announcements, and session rotation control messages. The client keeps the trust decision local: peer identity public keys are observed from online announcements, displayed as SHA-256 fingerprints, and can be pinned by the user.

## Current Rotation Boundary

- `E2EKeyAgreement` now carries both `senderIdentityFingerprintSha256` and `receiverIdentityFingerprintSha256` in addition to the rotation public key fingerprint.
- A client can send a rotation request or response only after it has observed the peer identity and that identity is not in `mismatch` state.
- A client accepts an incoming rotation message only when the sender identity fingerprint matches the locally observed peer identity and the receiver identity fingerprint matches the local identity.
- Rotation messages still do not install session keys automatically; the data plane remains fail-closed until both clients install matching local session keys.
- The server validates shape and routing, then forwards only public agreement material. It does not cache identity keys, session keys, or private material.

## Remaining Work

- Replace the placeholder public-key material with a real authenticated key agreement.
- Add signed identity keys and cross-device verification UX.
- Persist trusted identity pins with migration and recovery controls.
- Encrypt file content or chunks with the same fail-closed identity binding.
