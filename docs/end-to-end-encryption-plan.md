# End-to-End Encryption Hardening Plan

QtNetworkChat currently treats the server as a transparent carrier for private-message E2E envelopes, identity announcements, and session rotation control messages. The client keeps the trust decision local: peer identity public keys are observed from online announcements, displayed as SHA-256 fingerprints, and can be pinned by the user. Authenticated session setup now derives local session keys from identity-bound request/response transcripts, so the raw session key is never sent over the wire.

## Current Authenticated Agreement Boundary

- `E2EKeyAgreement` now carries both `senderIdentityFingerprintSha256` and `receiverIdentityFingerprintSha256` in addition to the rotation public key fingerprint.
- A client can send a rotation request or response only after it has observed the peer identity and that identity is not in `mismatch` state.
- A client accepts an incoming rotation message only when the sender identity fingerprint matches the locally observed peer identity and the receiver identity fingerprint matches the local identity.
- Rotation request/response messages carry only public agreement material. Each side keeps its private agreement scalar locally, validates the identity-bound transcript, and derives the same session key from the shared transcript before marking the data plane `ready`.
- Missing pending agreement state, mismatched peer IDs, mismatched identity fingerprints, invalid local private/public pairing, or malformed remote public material fail closed and do not install a session.
- The server validates shape and routing, then forwards only public agreement material. It does not cache identity keys, session keys, or private material.
- This is still a draft productization step using Qt primitives for testable authenticated agreement semantics; it is not yet a signed, cross-device, audited production cryptographic suite.

## Remaining Work

- Replace the draft agreement primitive with a reviewed production cryptographic backend and signed identity keys.
- Add cross-device verification UX.
- Persist trusted identity pins with migration and recovery controls.
- Add a default-enable policy with operator/user recovery gates.
- Migrate existing private-message history into explicit encrypted/plaintext states.
- Encrypt file content or chunks with the same fail-closed identity binding.
