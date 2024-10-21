#!/usr/bin/python3

import asyncio
import time
import json
import procmgr_py as pmgr
from munch import DefaultMunch

str_pmgr_defs = pmgr.get_defs()
pmgr_defs = DefaultMunch.fromDict(json.loads(str_pmgr_defs))
print(f"defs: {str_pmgr_defs}")

print("PMGR_CHAN_GET_IDENT", pmgr_defs.pmgr_msg_type_e.PMGR_CHAN_GET_IDENT)

async def main():
    print("a")
    try:
        print(await pmgr.example_awaitable(12))
    except Exception as e:
        print(f"Program excepted: {e} {repr(e)}")
    print("b")

asyncio.run(main())
