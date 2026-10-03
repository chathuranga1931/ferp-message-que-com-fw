"""Entry point:  python -m app   (run from tools/ferp-device-web/backend)."""

import logging

import uvicorn

from .server_settings import BACKEND_DIR


def main() -> None:
    try:
        from dotenv import load_dotenv
        load_dotenv(BACKEND_DIR / ".env")
    except ImportError:
        pass

    from .main import create_app
    from .server_settings import ServerSettings

    s = ServerSettings()
    logging.basicConfig(level=s.log_level.upper(), format="%(asctime)s %(levelname)-5s %(name)s: %(message)s")
    # Windows proactor loop logs a traceback whenever a browser drops a socket; it is harmless.
    logging.getLogger("asyncio").addFilter(
        lambda r: not (r.exc_info and isinstance(r.exc_info[1], ConnectionResetError)))
    # Single worker on purpose: MQTT connection, jobs and OTA sessions live in-process.
    uvicorn.run(create_app(s), host=s.host, port=s.port, log_level=s.log_level.lower(), workers=1)


if __name__ == "__main__":
    main()
