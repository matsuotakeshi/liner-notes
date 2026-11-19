"""Cross-language export: actual C codec SQL rows, old/new equality and corruption."""
from pathlib import Path
import contextlib,hashlib,io,sqlite3,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import structured_db

class Relations(unittest.TestCase):
    def test_export(self):
        binary=ROOT/('test-storage-relations.exe' if sys.platform=='win32' else 'test-storage-relations')
        with tempfile.TemporaryDirectory() as tmp:
            prefix=Path(tmp)/'schema'
            subprocess.run([str(binary),str(prefix)],check=True,capture_output=True)
            outputs=[]
            for version in (3,4):
                with contextlib.closing(sqlite3.connect(str(prefix)+str(version)+'.sqlite')) as db:
                    length=8+db.execute('select sum(length) from mail_records').fetchone()[0]
                    out=io.BytesIO();structured_db.export(db,1,2,b'LNMAIL01',length,out);outputs.append(out.getvalue())
                    self.assertEqual(db.execute('pragma integrity_check').fetchall(),[('ok',)])
                    self.assertEqual(db.execute('pragma foreign_key_check').fetchall(),[])
                    self.assertEqual(db.execute('select min(record_id),max(record_id),count(*) from mail_records').fetchone(),(100,116,17))
                    if version==4:
                        db.execute('delete from mail_attachments where offset=141')
                        with self.assertRaisesRegex(ValueError,'attachment relationship'):
                            structured_db.export(db,1,2,b'LNMAIL01',length,io.BytesIO())
            self.assertEqual(outputs[0],outputs[1])
            self.assertEqual(hashlib.sha256(outputs[0]).digest(),hashlib.sha256(outputs[1]).digest())

if __name__=='__main__':unittest.main()
