# End-to-End Encryption Hardening Plan

QtNetworkChat currently treats the server as a transparent carrier for private-message E2E envelopes, identity announcements, and session rotation control messages. The client keeps the trust decision local: peer identity public keys are observed from online announcements, displayed as SHA-256 fingerprints, and verified with a short cross-device code before the default encrypted data plane is allowed. Authenticated session setup now requires identity-signed request/response transcripts and derives local session keys without sending the raw session key over the wire.

## Current Authenticated Agreement Boundary

- `E2EKeyAgreement` now carries both `senderIdentityFingerprintSha256` and `receiverIdentityFingerprintSha256` in addition to the rotation public key fingerprint and a draft identity-bound agreement signature.
- A client can send a rotation request or response only after it has observed the peer identity and that identity is not in `mismatch` state.
- A client signs outgoing rotation requests/responses with its local persisted E2E identity material. Incoming rotation messages are accepted only when the sender identity fingerprint matches the locally observed peer identity, the receiver identity fingerprint matches the local identity, and the agreement signature verifies against the pinned sender identity public material.
- Rotation request/response messages carry only public agreement material. Each side keeps its private agreement scalar locally, validates the identity-bound transcript, and derives the same session key from the shared transcript before marking the data plane `ready`.
- Missing pending agreement state, mismatched peer IDs, mismatched identity fingerprints, missing/tampered agreement signatures, invalid local private/public pairing, or malformed remote public material fail closed and do not install a session.
- The server validates shape and routing, then forwards only public agreement material. It does not cache identity keys, session keys, or private material.
- This is still a draft productization step using Qt primitives for testable authenticated agreement semantics. It is now signature-gated and cross-device-code gated at the client protocol boundary, but still needs a reviewed production cryptographic backend before it should be treated as an audited production suite.

## Current Cross-Device Trust Boundary

- Local E2E identity private material is persisted per local account under the app data directory, so a restart keeps the same advertised public-key fingerprint instead of breaking previously pinned peers.
- Each side computes the same short cross-device verification code from the local and peer identity fingerprints. The contact context menu can copy the code for comparison over a trusted channel, or accept an entered code to promote a pinned identity to `trusted`.
- Peer trust pins are persisted per local account as SHA-256 fingerprints plus verification state, verification code, and timestamps. The trust store does not contain peer public keys, local private agreement material, session keys, passwords, tokens, or server endpoints.
- A newly observed peer identity is automatically marked `trusted` only when it matches a persisted verified pin and the verification code still matches; unverified pins stay `pending-verification`, and conflicting fingerprints become `mismatch`.
- The contact context menu can clear a persisted trust pin and recover the peer to `unverified` without deleting the latest observed identity.
- The default data-plane policy requires a verified, trusted, non-mismatched peer identity before starting key agreement, accepting key agreement, or sending encrypted private messages. Stale sessions are not enough after trust recovery clears a pin.
- Corrupted local identity stores are regenerated with a fresh persisted identity, while malformed trust pins are ignored instead of being treated as trusted.

## Current History Metadata Boundary

- The local `chat_history` table is migrated in place with `encryption_state`, `e2e_key_id`, and `e2e_key_fingerprint` columns.
- Legacy rows with no encryption metadata are normalized to `plaintext`; new encrypted private-message rows are stored as `encrypted`, and valid envelopes that cannot decrypt are stored as `decrypt-failed`.
- History loading, date filtering, legacy text fallback, and export formatting now show explicit E2E state labels for encrypted or failed records while leaving plaintext rows visually unchanged.
- History metadata stores the key id and a short display fingerprint derived from the local session status when it matches the envelope key id. Raw session keys, private agreement material, peer public keys, passwords, tokens, and full fingerprints are not exported through this formatting path.
- This is metadata governance for local history visibility and auditability. It does not retroactively encrypt old plaintext history rows.

## Current File Payload Boundary

- Private file and image sends now use the same verified, trusted, non-mismatched peer identity and ready local E2E session as encrypted private text by default. Missing session, unverified identity, mismatch, or rotation-required state fails closed instead of silently sending private file payloads as plaintext.
- The sender encrypts the whole file payload into one E2E envelope, then sends the opaque ciphertext through the existing file chunk path. The server can validate and store only ciphertext size/hash and transfer metadata.
- The receiver collects the ciphertext chunks, authenticates the E2E envelope locally, decrypts the plaintext payload, and verifies the declared plaintext size and SHA-256 hash before surfacing the file message.
- Missing trust on an existing session, rotation-required sessions, invalid envelopes, authentication failure, plaintext size mismatch, or plaintext hash mismatch fail closed. Raw session keys, private agreement material, plaintext payloads, and full key material are not written into wire status or server logs.
- E2E file resume is currently conservative: interrupted encrypted private file sends must be resent so the ciphertext and envelope stay consistent. Group files, Redis/S3 object routing, and production crypto replacement remain separate productization work. Protocol/operations tests that intentionally exercise legacy plaintext private-file routing must opt in with `QTNETWORKCHAT_E2E_ALLOW_PLAINTEXT_PRIVATE_FILE=1`; production defaults keep this path closed.

## Remaining Work

- Replace the draft agreement/signature primitive with a reviewed production cryptographic backend and durable signed identity keys.
- Add migration checks for older identity/pin store schemas and richer operator/user recovery prompts.
- Extend E2E file recovery and large-object routing evidence beyond the current resend-on-interruption boundary.
