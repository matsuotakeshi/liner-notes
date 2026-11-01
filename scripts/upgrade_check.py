"""Read-only upgrade inventory. Never replaces configuration or starts a daemon."""
import argparse,json,os,stat,sqlite3
from pathlib import Path

def assets(roots):
    result=[]
    for root in roots:
        root=Path(root)
        if not root.is_dir() or root.is_symlink():raise ValueError('Missing or unsafe asset root: '+str(root))
        for directory,dirs,files in os.walk(root,followlinks=False):
            dirs[:]=[x for x in dirs if not (Path(directory)/x).is_symlink()]
            for name in sorted(files):
                if not name.endswith('.sample'):continue
                sample=Path(directory)/name;installed=sample.with_name(name[:-7])
                if sample.is_symlink() or not sample.is_file():raise ValueError('Unsafe sample: '+str(sample))
                state='missing'
                if installed.exists() or installed.is_symlink():
                    if installed.is_symlink() or not installed.is_file():state='unsafe'
                    elif installed.read_bytes()==sample.read_bytes():state='matches-sample'
                    else:state='review-required'
                result.append(dict(file=str(installed),sample=str(sample),status=state))
    return result

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',action='append',default=[])
    p.add_argument('--database')
    a=p.parse_args(argv)
    if not a.assets and not a.database:p.error('Specify --assets and/or --database')
    try:
        result=dict(operation='upgrade-check',read_only=True,assets=assets(a.assets))
        if a.database:
            path=Path(a.database).absolute();st=path.lstat()
            if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1:raise ValueError('Unsafe database')
            with sqlite3.connect(path.as_uri()+'?mode=ro',uri=True) as db:
                version=db.execute('SELECT version FROM storage_meta WHERE id=1').fetchone()
                if version not in ((1,),(2,),(3,)):raise ValueError('Unsupported database schema')
                result['database']=dict(schema=version[0],supported=True,automatic_conversion=False)
        print(json.dumps(result,ensure_ascii=True,indent=2))
    except (OSError,ValueError,sqlite3.Error) as error:p.exit(1,str(error)+'\n')

if __name__=='__main__':main()
