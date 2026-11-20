"""Real C-created schema-5 records, independent Python export and atomic copy."""
from pathlib import Path
import contextlib,hashlib,io,os,sqlite3,subprocess,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import database_tools,normalized_fields,structured_db

class Normalized(unittest.TestCase):
    def test_cross_language_all_domains(self):
        binary=ROOT/('test-storage-normalized.exe' if os.name=='nt' else 'test-storage-normalized')
        with tempfile.TemporaryDirectory() as tmp:
            prefix=Path(tmp)/'fixture'
            subprocess.run([str(binary),str(prefix)],check=True,capture_output=True)
            original=prefix.with_suffix('.sqlite').read_bytes()
            with contextlib.closing(sqlite3.connect(prefix.with_suffix('.sqlite'))) as db:
                self.assertEqual(normalized_fields.validate_relations(db)['member_terminal'],1)
                for kind in (1,3,17):
                    expected=Path(str(prefix)+f'-{kind}.raw').read_bytes()
                    out=io.BytesIO();structured_db.export(db,kind,kind,expected[:8],len(expected),out)
                    self.assertEqual(out.getvalue(),expected)
                for table in ('member_records','article_records'):
                    self.assertEqual(db.execute('SELECT sum(length(attributes)) FROM '+table).fetchone(),(0,))
                self.assertEqual(db.execute('SELECT group_id FROM member_event_groups WHERE offset=776 ORDER BY group_id').fetchall(),[(1,),(32,),(33,),(96,),(100,)])
                self.assertEqual(db.execute('PRAGMA integrity_check').fetchall(),[('ok',)])
                self.assertEqual(db.execute('PRAGMA foreign_key_check').fetchall(),[])
                self.assertEqual(db.execute('SELECT max(length(body)) FROM article_records').fetchone(),(262144,))
                self.assertEqual(db.execute('SELECT iterations,length(password_hash) FROM member_registration JOIN member_records USING(stream,offset) ORDER BY offset LIMIT 1').fetchone(),(600000,32))
                # Parent ID overflow must never alias a legitimate 32-bit ID.
                db.execute('UPDATE article_records SET record_id=4294967297 WHERE offset=8')
                with self.assertRaisesRegex(ValueError,'identity'):
                    structured_db.export(db,1,1,b'RTBOARD5',1,io.BytesIO())
                db.rollback()
                db.execute('DELETE FROM member_terminal')
                with self.assertRaisesRegex(ValueError,'normalized relation'):
                    normalized_fields.validate_relations(db)
                expected=Path(str(prefix)+'-3.raw').read_bytes()
                with self.assertRaisesRegex(ValueError,'Missing normalized'):
                    structured_db.export(db,3,3,expected[:8],len(expected),io.BytesIO())
                db.rollback()
                db.execute("UPDATE vault_records SET name='changed' WHERE offset=8")
                with self.assertRaisesRegex(ValueError,'checksum'):
                    structured_db.export(db,17,17,b'LNVLT001',648,io.BytesIO())
                db.rollback()
            self.assertEqual(prefix.with_suffix('.sqlite').read_bytes(),original)

    def test_generated_descriptors_current(self):
        self.assertEqual((ROOT/'src/storage_normalized_fields.inc').read_text(),normalized_fields.c_source())

    def test_failed_conversion_never_publishes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=root/'notes';source.write_bytes(b'original')
            def broken(*args,**kwargs):
                kwargs['_target'].write_bytes(b'incomplete')
                raise OSError('injected disk full')
            with patch.object(database_tools,'_import_copy',side_effect=broken):
                with self.assertRaises(OSError):database_tools.import_copy(root,'notes','members',structured=True)
            self.assertFalse((root/'liner.sqlite').exists())
            self.assertEqual(source.read_bytes(),b'original')
            self.assertEqual(list(root.glob('.db-import-*')),[])

    def test_no_clobber_publication(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);final=root/'liner.sqlite'
            def build(*args,**kwargs):
                kwargs['_target'].write_bytes(b'validated')
                final.write_bytes(b'concurrent owner')
                return {'database':'temporary'}
            with patch.object(database_tools,'_import_copy',side_effect=build):
                with self.assertRaises(FileExistsError):database_tools.import_copy(root,'notes','members')
            self.assertEqual(final.read_bytes(),b'concurrent owner')

    @unittest.skipIf(os.name=='nt','POSIX fsync directory is exercised by Hosted/native CI')
    def test_actual_import_checkpoint_and_publication(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'notes').write_bytes(b'RTBOARD4');(root/'members').write_bytes(b'RTMEMB01')
            with contextlib.closing(sqlite3.connect(root/'notes.features.sqlite')) as features:
                features.execute('CREATE TABLE ui_preferences(user INTEGER PRIMARY KEY,profile TEXT NOT NULL)')
                features.execute("INSERT INTO ui_preferences VALUES(42,'rtbbs')");features.commit()
            # Exercise the real importer with WAL mode, even on the blocks path.
            connect=sqlite3.connect
            def wal_connect(*args,**kwargs):
                db=connect(*args,**kwargs)
                if str(args[0]).endswith('candidate.sqlite'):db.execute('PRAGMA journal_mode=WAL')
                return db
            with patch.object(database_tools.sqlite3,'connect',side_effect=wal_connect):
                result=database_tools.import_copy(root,'notes','members')
            self.assertEqual(result['streams'],2)
            self.assertEqual(list(root.glob('.db-import-*')),[])
            self.assertFalse((root/'liner.sqlite-wal').exists())
            # A fresh process must see both settings and stream bytes without WAL.
            script="import sqlite3,sys; d=sqlite3.connect(sys.argv[1]); assert d.execute('SELECT profile FROM ui_preferences WHERE user=42').fetchone()==('rtbbs',); assert d.execute('SELECT count(*) FROM streams').fetchone()==(2,); assert d.execute('PRAGMA integrity_check').fetchone()==('ok',)"
            subprocess.run([sys.executable,'-c',script,str(root/'liner.sqlite')],check=True)

if __name__=='__main__':unittest.main()
