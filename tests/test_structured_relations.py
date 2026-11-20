"""Cross-language export: actual C codec SQL rows, old/new equality and corruption."""
from pathlib import Path
import contextlib,hashlib,io,shutil,sqlite3,subprocess,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import structured_db,database_tools

class Relations(unittest.TestCase):
    def test_import_legacy_converter_output(self):
        """Use real C codec output; substitute only the external converter process."""
        binary=ROOT/('test-storage-relations.exe' if sys.platform=='win32' else 'test-storage-relations')
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);prefix=base/'schema'
            subprocess.run([str(binary),str(prefix)],check=True,capture_output=True)
            for version in (2,3,4,5):
                fixture=Path(str(prefix)+str(version)+'.sqlite')
                with contextlib.closing(sqlite3.connect(fixture)) as db, db:
                    length=8+db.execute('SELECT sum(length) FROM mail_records').fetchone()[0]
                    mail=io.BytesIO();structured_db.export(db,1,2,b'LNMAIL01',length,mail)
                    db.executescript('ALTER TABLE streams ADD COLUMN name TEXT; ALTER TABLE streams ADD COLUMN length INTEGER; ALTER TABLE streams ADD COLUMN revision INTEGER DEFAULT 1; CREATE TABLE blocks(stream INTEGER,number INTEGER,value BLOB,PRIMARY KEY(stream,number));')
                    db.execute("UPDATE streams SET name='mail',length=? WHERE id=1",(length,))
                    for ident,name,kind,magic in ((2,'notes',1,b'RTBOARD5'),(3,'members',3,b'RTMEMB01')):
                        db.execute('INSERT INTO streams(id,name,length) VALUES(?,?,8)',(ident,name))
                        db.execute('INSERT INTO record_streams VALUES(?,?,?)',(ident,kind,magic))
                cases=['valid','hash-mismatch','unknown-version']
                if version>=4:cases.append('missing-attachment')
                if version==5:cases.append('missing-normalized-table')
                for case in cases:
                    with self.subTest(version=version,case=case):
                        root=base/f'copy-{version}-{case}';root.mkdir()
                        streams={'notes':b'RTBOARD5','members':b'RTMEMB01','mail':mail.getvalue()}
                        if case=='hash-mismatch':streams['mail']+=b'tampered'
                        for name,data in streams.items():(root/name).write_bytes(data)
                        with contextlib.closing(sqlite3.connect(root/'notes.features.sqlite')) as settings, settings:
                            settings.execute('CREATE TABLE ui_preferences(user INTEGER PRIMARY KEY,profile TEXT NOT NULL)')
                            settings.execute("INSERT INTO ui_preferences VALUES(42,'rtbbs')")
                        tools=root/'tools';tools.mkdir();converter=tools/'dbconvert'
                        converter.write_bytes(b'fixture process port');converter.chmod(0o755)
                        def converted(argv,**kwargs):
                            self.assertEqual(Path(argv[0]),converter.resolve())
                            self.assertEqual(set(kwargs['input'].decode().splitlines()),set(streams))
                            shutil.copyfile(fixture,argv[1])
                            with contextlib.closing(sqlite3.connect(argv[1])) as out,out:
                                if case=='unknown-version':out.execute('UPDATE storage_meta SET version=99')
                                if case=='missing-normalized-table':out.execute('DROP TABLE article_format')
                                if case=='missing-attachment':out.execute('DELETE FROM mail_attachments WHERE offset=141')
                            return subprocess.CompletedProcess(argv,0)
                        with patch.object(database_tools.subprocess,'run',side_effect=converted):
                            if case=='valid':
                                result=database_tools.import_copy(root,'notes','members',structured=True,tools_dir=tools)
                                self.assertEqual(result['streams'],3)
                                self.assertEqual(result['schema'],version)
                                with contextlib.closing(sqlite3.connect(root/'liner.sqlite')) as final:
                                    self.assertEqual(final.execute('SELECT version FROM storage_meta').fetchone(),(version,))
                                    self.assertEqual(final.execute('SELECT profile FROM ui_preferences WHERE user=42').fetchone(),('rtbbs',))
                                    self.assertEqual(final.execute('SELECT count(*) FROM mail_records').fetchone(),(17,))
                                    rebuilt=io.BytesIO();structured_db.export(final,1,2,b'LNMAIL01',length,rebuilt)
                                    self.assertEqual(rebuilt.getvalue(),streams['mail'])
                            else:
                                expected={'hash-mismatch':'Migration hash mismatch','unknown-version':'Unsupported converter storage schema','missing-attachment':'attachment relationship|attachment count','missing-normalized-table':'article_format'}[case]
                                with self.assertRaisesRegex((ValueError,sqlite3.DatabaseError),expected):
                                    database_tools.import_copy(root,'notes','members',structured=True,tools_dir=tools)
                                self.assertFalse((root/'liner.sqlite').exists())
                        self.assertEqual(list(root.glob('.db-import-*')),[])
                        for name,data in streams.items():self.assertEqual((root/name).read_bytes(),data)

    def test_export(self):
        binary=ROOT/('test-storage-relations.exe' if sys.platform=='win32' else 'test-storage-relations')
        with tempfile.TemporaryDirectory() as tmp:
            prefix=Path(tmp)/'schema'
            subprocess.run([str(binary),str(prefix)],check=True,capture_output=True)
            outputs=[]
            for version in (2,3,4,5):
                with contextlib.closing(sqlite3.connect(str(prefix)+str(version)+'.sqlite')) as db:
                    length=8+db.execute('select sum(length) from mail_records').fetchone()[0]
                    out=io.BytesIO();structured_db.export(db,1,2,b'LNMAIL01',length,out);outputs.append(out.getvalue())
                    self.assertEqual(db.execute('pragma integrity_check').fetchall(),[('ok',)])
                    self.assertEqual(db.execute('pragma foreign_key_check').fetchall(),[])
                    self.assertEqual(db.execute('select min(record_id),max(record_id),count(*) from mail_records').fetchone(),(100,116,17))
                    if version>=4:
                        db.execute('delete from mail_attachments where offset=141')
                        with self.assertRaisesRegex(ValueError,'attachment relationship'):
                            structured_db.export(db,1,2,b'LNMAIL01',length,io.BytesIO())
            self.assertTrue(all(value==outputs[0] for value in outputs))
            self.assertEqual(hashlib.sha256(outputs[0]).digest(),hashlib.sha256(outputs[1]).digest())

if __name__=='__main__':unittest.main()
