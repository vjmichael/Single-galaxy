#!/usr/bin/env python3
"""Normalize the *user-supplied* broken CSV without upgrading unverified claims.
68 numbered physical records became one CSV row because each reason field opened
an unterminated quote. Preserve all fields and source status strings as supplied.
"""
from pathlib import Path
import csv
from collections import Counter

base=Path(__file__).resolve().parent
original=base/'evemu_client_server_calls_original.csv'
out=base/'evemu_client_server_calls_normalized.csv'
lines=original.read_text(encoding='utf-8').splitlines()
header=next(csv.reader([lines[0]]))
assert len(header)==10 and len(lines)==69
parsed=[]
for n,line in enumerate(lines[1:],1):
    if n>1:
        assert line.startswith(str(n)+'"'),(n,line[:40])
        line=str(n)+line[len(str(n))+1:]
    # Source omitted a terminating quote on every free-text notes field.
    if not line.endswith('"'):line+='"'
    fields=next(csv.reader([line]))
    if n==2:
        assert len(fields)==11 and fields[6]=='src/eve-server/Client.cpp _VerifyCrypto'
        fields[6]=fields[6]+';'+fields.pop(7)
    if 59<=n<=65:
        assert len(fields)==9 and fields[7] in ('CONFIRMED WORKING','PARTIAL')
        fields.insert(7,'') # no XMLP definition provided, do not invent one
    assert len(fields)==10,(n,len(fields),fields)
    assert fields[0]==str(n)
    parsed.append(fields)
with out.open('w',newline='',encoding='utf8') as f:
    writer=csv.writer(f)
    writer.writerow(header);writer.writerows(parsed)
with out.open(newline='',encoding='utf8') as f:
    verified=list(csv.reader(f))
assert len(verified)==69 and all(len(r)==10 for r in verified)
print('Normalized',len(parsed),'supplied records; statuses unchanged:',dict(Counter(x[8] for x in parsed)))
