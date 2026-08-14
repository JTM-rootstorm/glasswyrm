# X11 transition fixtures

These byte streams freeze the accepted legacy `src/protocol/x11` behavior for
the first Rust `glasswyrm-x11` migration slice. The setup success and failure
records are the existing byte-exact M1 goldens. Request, reply, and error
records use the exact values exercised by the existing legacy protocol tests.

The fixtures cover both X11 byte orders. They are compatibility anchors, not a
claim that unsupported X11 protocol surface is implemented.

Verify the frozen bytes with:

```sh
cd tests/fixtures/x11-transition
sha256sum --check SHA256SUMS
```
