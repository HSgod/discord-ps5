# TLS test certificates

Fixtures for `tests/net/tls_stream_test.cpp`: the local server in
`tests/support/tls_server.cpp` presents one of these, and the client either has
to accept it or has to refuse it.

| file | what it is |
| --- | --- |
| `ca.pem` | the test CA; what the tests hand the client as its trust store |
| `ca.key` | the CA's key, used to sign the server certificate |
| `server-localhost.crt` / `.key` | `CN=localhost` with `DNS:localhost,IP:127.0.0.1`, signed by the CA -- the certificate a client has to accept |
| `server-selfsigned.crt` / `.key` | the same name and the same extensions, signed by itself -- the certificate a client has to refuse for the chain alone |

Nothing here is a secret and none of it belongs to anyone. The keys sign a
server on loopback that exists only while a test runs, which is what makes it
reasonable to commit them: the tests then need no setup step, and a checkout
that cannot reach the network can still check the one thing this layer is for.

The certificates last twenty years, so they do not quietly expire between runs.

Regenerate them with:

```sh
tools/make-test-certs.sh
```

That script uses `openssl` from `PATH`, overwrites every file in this directory,
and prints `server-localhost.crt: OK` when the CA verifies what it signed.
