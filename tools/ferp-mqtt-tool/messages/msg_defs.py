"""
msg_defs.py — Re-exports from msg_loader for backward compatibility.

All message definitions are now loaded dynamically from the shared JSON files in
src/app-messages/messages/.  To add or modify messages edit the JSON files there;
no changes to this file are needed.
"""
from messages.msg_loader import CMD_MSGS, RESP_MSGS, ALL_MSGS, MSG_DEFS  # noqa: F401
