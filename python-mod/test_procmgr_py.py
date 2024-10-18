#!/usr/bin/python3

import asyncio
import time
import procmgr_py as pmgr

async def main():
    print("a")
    print(await pmgr.example_awaitable(12))
    print("b")

asyncio.run(main())
