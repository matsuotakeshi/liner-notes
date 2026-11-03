import argparse
from pathlib import Path
import tomllib
from aiohttp import web
from .app import create_app

parser=argparse.ArgumentParser(description='Liner notes Web connector')
parser.add_argument('--config',type=Path,required=True)
args=parser.parse_args()
with args.config.open('rb') as stream:config=tomllib.load(stream)
for name in ('messages','templates','spool'):
    if name in config['web']:
        config['web'][name]=str((args.config.resolve().parent/config['web'][name]).resolve())
app=create_app(config)
# Do not log query strings: WebSocket authorization data must not enter access logs.
web.run_app(app,host=config['web'].get('listen','127.0.0.1'),port=config['web'].get('port',8088),access_log=None)
