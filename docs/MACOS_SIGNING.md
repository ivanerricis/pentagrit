# macOS signing and notarization (phase 2)

Today CI produces **ad-hoc signed** macOS binaries, which Gatekeeper flags when downloaded. Users must run `xattr -dr com.apple.quarantine <bundle>`. Phase 2 adds Developer ID signing and notarization. It requires a paid Apple Developer Program account.

## Prerequisites

- Developer ID Application certificate (and Developer ID Installer for `.pkg`), exported as a `.p12`.
- An app-specific password for the Apple ID (appleid.apple.com).
- Team ID from the Developer portal.

## GitHub secrets

| Secret | Content |
|---|---|
| `MACOS_CERT_P12_BASE64` | `base64 -i cert.p12` of the Developer ID certificate |
| `MACOS_CERT_PASSWORD` | Password of the `.p12` |
| `APPLE_ID` | Apple ID email |
| `APPLE_TEAM_ID` | 10-character Team ID |
| `APPLE_APP_PASSWORD` | App-specific password |

## Steps in CI (macOS runner)

```
# 1. Import the certificate into a temporary keychain
echo "$MACOS_CERT_P12_BASE64" | base64 --decode > cert.p12
security create-keychain -p "$KC_PASS" build.keychain
security default-keychain -s build.keychain
security unlock-keychain -p "$KC_PASS" build.keychain
security import cert.p12 -k build.keychain -P "$MACOS_CERT_PASSWORD" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple: -s -k "$KC_PASS" build.keychain

# 2. Sign every bundle (hardened runtime + secure timestamp)
codesign --force --deep --options runtime --timestamp \
  --sign "Developer ID Application: <Name> ($APPLE_TEAM_ID)" Pentagrit.vst3
# repeat for .component, .clap, .app

# 3. Zip and notarize
ditto -c -k --keepParent Pentagrit.vst3 Pentagrit.vst3.zip
xcrun notarytool submit Pentagrit.vst3.zip \
  --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait

# 4. Staple the ticket
xcrun stapler staple Pentagrit.vst3
```

Verify with `codesign --verify --deep --strict -vv` and `spctl -a -vv -t install`.

## Optional signed installer

Build a `.pkg` with `pkgbuild`/`productbuild`, sign it with `--sign "Developer ID Installer: ..."`, notarize with `notarytool submit --wait` and `stapler staple` the `.pkg`. This installs the bundles in the system plug-in folders without manual steps.

## Notes

- Never commit certificates or passwords; use repository secrets only, and delete the temporary keychain at the end of the job.
- Re-run `auval` after signing.

## Signing the `.pkg` installer

`scripts/ci/make_installer_macos.sh` builds `Pentagrit-vX.Y.Z-macos.pkg` unsigned by default. To sign it, export the Developer ID **Installer** certificate (separate from the Application one) and set, in the macOS build step:

```
MACOS_INSTALLER_CERT="Developer ID Installer: <Name> (<TEAMID>)"   # enables productbuild --sign
MACOS_INSTALLER_KEYCHAIN=build.keychain                            # optional
```

Then notarize and staple the package itself:

```
xcrun notarytool submit Pentagrit-vX.Y.Z-macos.pkg --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait
xcrun stapler staple Pentagrit-vX.Y.Z-macos.pkg
```

Bundles inside the `.pkg` should be Developer ID signed first (steps above), since the installer copies the signed bundles from the zip.
