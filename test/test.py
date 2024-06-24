#!/usr/bin/env python

import base64
import hashlib
import hmac

SECRET = "foobar"
RAW_DATA = "1234567890"

digest = hmac.new(
    bytes(SECRET, "utf-8"),
    msg=bytes(RAW_DATA, "utf-8"),
    digestmod=hashlib.sha256,
).digest()

signature = base64.b64encode(digest)

print(signature)
