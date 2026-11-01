On LP64, complementing unsigned long also sets its upper 32 bits.
ZCRC is 32 bits; mask the CRC so matching partial files can resume.

--- src/lrz.c.orig
+++ src/lrz.c
@@ -929,7 +929,7 @@
         n=st.st_size;
     while (n-- && ((c = getc(f)) != EOF))
         crc = UPDC32(c, crc);
-    crc = ~crc;
+    crc = (~crc) & 0xffffffffUL;
     clearerr(f);  /* Clear EOF */
     fseeko(f, 0, 0);
 
