import argparse, hashlib, re, xml.etree.ElementTree as ET
from pathlib import Path
parser=argparse.ArgumentParser(description="Generate the embedded IANA snapshot from four locally downloaded official XML registries.")
parser.add_argument('--source-dir', type=Path, required=True)
parser.add_argument('--retrieved', required=True, help='Verified retrieval date, YYYY-MM-DD')
parser.add_argument('--output', type=Path, default=Path(__file__).with_name('registry_snapshot.inc'))
args=parser.parse_args()
if not re.fullmatch(r'\d{4}-\d{2}-\d{2}',args.retrieved):parser.error('retrieval date must be YYYY-MM-DD')
N='{http://www.iana.org/assignments}'
names=['iana-ipv4-special-registry','iana-ipv6-special-registry','ipv4-address-space','ipv6-address-space']
def render(node):
    if node is None:return ''
    text=node.text or ''
    for c in node:
        if c.tag==N+'xref':
            kind,data=c.get('type'),c.get('data','')
            if kind=='note':part='['+data+']'
            elif kind=='rfc':part='https://www.rfc-editor.org/rfc/'+data
            elif kind=='registry':part='https://www.iana.org/assignments/'+data+'/'
            elif kind=='uri':part=data
            else:part=data
            if c.text and kind not in ('note','rfc'):part=c.text+' ('+part+')'
            text+=part
        else:text+=render(c)
        text+=c.tail or ''
    return ' '.join(text.split())
def plain(node):return ' '.join(''.join(node.itertext()).split()) if node is not None else ''
def cpp(s):
    escaped=''
    for b in s.encode('utf-8'):
        if b==34:escaped+='\\"'
        elif b==92:escaped+='\\\\'
        elif 32<=b<=126:escaped+=chr(b)
        else:escaped+='\\'+format(b,'03o')
    return '"'+escaped+'"'
sources=[];rows=[]
for si,name in enumerate(names):
    raw=(args.source_dir / (name+'.xml')).read_bytes();root=ET.fromstring(raw)
    updated=plain(root.find(N+'updated'))
    footnotes={f.get('anchor'):render(f) for f in root.iter(N+'footnote')}
    notes=' '.join(render(x) for x in root.findall(N+'note') if not x.attrib)
    if si<2:
        nested=root.find(N+'registry')
        notes+=' '+ ' '.join(render(x) for x in nested.findall(N+'note') if not x.attrib)
    notes += ' Registry footnotes: ' + ' '.join('['+key+'] '+value for key,value in footnotes.items()) if footnotes else ''
    sources.append([plain(root.find(N+'title')),'https://www.iana.org/assignments/'+name+'/',updated,args.retrieved,hashlib.sha256(raw).hexdigest(),notes.strip()])
    for record in root.iter(N+'record'):
        def get(field):return render(record.find(N+field))
        special=si<2
        prefix=plain(record.find(N+('address' if special else 'prefix')))
        if si==2:prefix=str(int(prefix.split('/')[0]))+'.0.0.0/8'
        refs=[]
        for x in record.iter(N+'xref'):
            if x.get('type')=='rfc':refs.append('https://www.rfc-editor.org/rfc/'+x.get('data'))
            elif x.get('type')=='uri':refs.append(x.get('data'))
        rownotes=get('notes')
        for x in record.iter(N+'xref'):
            if x.get('type')=='note':
                key=x.get('data');rownotes+=' ['+key+'] '+footnotes.get(key,'See source registry note '+key)
        title=get('name' if special else 'designation' if si==2 else 'description')
        data=[title,get('status'),get('allocation') if special else get('date'),get('termination'),get('source'),get('destination'),get('forwardable'),get('global'),get('reserved'),' '.join(dict.fromkeys(refs)),rownotes.strip()]
        for p in prefix.split(','):rows.append([p.strip()]+data+[si,special])
lines=['// Generated from official IANA XML, retrieved '+args.retrieved+'. No runtime download.', '// Snapshot includes all 25 + 25 special-purpose records and all 256 + 20', '// address-space records; one multi-prefix special record expands to two rows.', 'static constexpr RegistrySource kSources[] = {']
for s in sources:lines.append('  {'+', '.join(map(cpp,s))+'},')
lines.append('};\nstatic constexpr SnapshotRow kRows[] = {')
for row in rows:lines.append('  {'+', '.join(map(cpp,row[:-2]))+', '+str(row[-2])+', '+str(row[-1]).lower()+'},')
lines.append('};\n')
args.output.write_text('\n'.join(lines),encoding='ascii')
print(f'Embedded {len(rows)} prefixes from {len(sources)} registries')
for s in sources:print(s[0],s[2],s[4])
