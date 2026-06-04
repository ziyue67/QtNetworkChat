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

## Current Trust Persistence Boundary

- Local E2E identity private material is persisted per local account under the app data directory, so a restart keeps the same advertised public-key fingerprint instead of breaking previously pinned peers.
- Peer trust pins are persisted per local account as SHA-256 fingerprints only. The trust store does not contain peer public keys, local private agreement material, session keys, passwords, tokens, or server endpoints.
- A newly observed peer identity is automatically marked `trusted` when it matches a persisted pin, and `mismatch` when it conflicts with the persisted pin.
- The contact context menu can clear a persisted trust pin and recover the peer to `unverified` without deleting the latest observed identity.
- The default data-plane policy requires a trusted, non-mismatched peer identity before starting key agreement, accepting key agreement, or sending encrypted private messages. Stale sessions are not enough after trust recovery clears a pin.
- Corrupted local identity stores are regenerated with a fresh persisted identity, while malformed trust pins are ignored instead of being treated as trusted.

## Current History Metadata Boundary

- The local `chat_history` table is migrated in place with `encryption_state`, `e2e_key_id`, and `e2e_key_fingerprint` columns.
- Legacy rows with no encryption metadata are normalized to `plaintext`; new encrypted private-message rows are stored as `encrypted`, and valid envelopes that cannot decrypt are stored as `decrypt-failed`.
- History loading, date filtering, legacy text fallback, and export formatting now show explicit E2E state labels for encrypted or failed records while leaving plaintext rows visually unchanged.
- History metadata stores the key id and a short display fingerprint derived from the local session status when it matches the envelope key id. Raw session keys, private agreement material, peer public keys, passwords, tokens, and full fingerprints are not exported through this formatting path.
- This is metadata governance for local history visibility and auditability. It does not retroactively encrypt old plaintext history rows.

## Current File Payload Boundary

- Private file and image sends automatically use the same trusted, non-mismatched peer identity and ready local E2E session as encrypted private text when such a session already exists. Legacy private file sends without any local E2E session remain compatible with the existing plaintext file path until the default product policy is tightened.
- The sender encrypts the whole file payload into one E2E envelope, then sends the opaque ciphertext through the existing file chunk path. The server can validate and store only ciphertext size/hash and transfer metadata.
- The receiver collects the ciphertext chunks, authenticates the E2E envelope locally, decrypts the plaintext payload, and verifies the declared plaintext size and SHA-256 hash before surfacing the file message.
- Missing trust on an existing session, rotation-required sessions, invalid envelopes, authentication failure, plaintext size mismatch, or plaintext hash mismatch fail closed. Raw session keys, private agreement material, plaintext payloads, and full key material are not written into wire status or server logs.
- E2E file resume is currently conservative: interrupted encrypted private file sends must be resent so the ciphertext and envelope stay consistent. Group files, Redis/S3 object routing, and production crypto replacement remain separate productization work.

## Remaining Work

- Replace the draft agreement primitive with a reviewed production cryptographic backend and signed identity keys.
- Add cross-device verification UX.
- Add migration checks for older identity/pin store schemas and richer operator/user recovery prompts.
- Tighten the default private file policy from opportunistic E2E when a session exists to mandatory E2E once cross-device recovery and onboarding UX are ready.
- Extend E2E file recovery and large-object routing evidence beyond the current resend-on-interruption boundary.
