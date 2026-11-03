import datetime
import json
from pathlib import Path
from zoneinfo import ZoneInfo


class Presentation:
    def __init__(self, directory, timezone):
        self.directory = Path(directory)
        self.timezone = ZoneInfo(timezone)

    def read(self, language, handle=''):
        language = 'en' if language=='en' else 'ja'
        path = self.directory / (language+'.json')
        if path.is_symlink() or path.stat().st_size>65536: raise ValueError('Invalid presentation file')
        content = json.loads(path.read_text(encoding='utf-8'))
        now = datetime.datetime.now(self.timezone)
        values = {'handle':handle, 'date':now.strftime('%Y-%m-%d'), 'time':now.strftime('%H:%M')}
        def render(value):
            if not isinstance(value,str): raise ValueError('Presentation messages must be text')
            for key,text in values.items():value=value.replace('{{'+key+'}}',text)
            return value
        result = {key:render(content.get(key,'')) for key in ('name','tagline','banner','welcome','news','goodbye','system','birthday')}
        greetings=content.get('greetings',{})
        valid=sorted((int(hour),text) for hour,text in greetings.items() if hour.isdigit() and 0<=int(hour)<=23)
        choices=[text for hour,text in valid if hour<=now.hour]
        result['greeting']=render(choices[-1] if choices else valid[-1][1] if valid else '')
        result['time']=now.isoformat()
        return result
