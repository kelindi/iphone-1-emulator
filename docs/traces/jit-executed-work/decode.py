"""Pure retained-byte decoder. No guest, MMU access or guessed routine binding."""
import struct,collections

def decode(rows,generated,summary,meta):
    assert len(rows)==meta['interpreterCapacity']*40,'row extent'
    assert len(generated)==meta['generatedCapacity']*8==512*512*8,'generated extent'
    assert len(summary)==80,'summary extent'
    values=struct.unpack('<9Q2I',summary)
    names=['calls','ok','fetched','nofetch','nonok','overflow','mapOverflow','epoch','installs','rows','enabled']
    totals=dict(zip(names,values));assert totals['enabled']==0,'capture not stopped'
    assert totals['epoch']==int(meta['epochStart']),'capture reset'
    assert not(totals['overflow']or totals['mapOverflow']or totals['installs']or int(meta['generatedOverflow'])),'observation loss/lifecycle failure'
    positions={}
    for pos in meta['positions']:
        slot=pos['slot'];assert isinstance(slot,int) and 0<=slot<meta['generatedCapacity'] and slot not in positions,'ambiguous generated slot'
        assert isinstance(pos['thumb'],bool) and pos['pc']%(2 if pos['thumb'] else 4)==0,'position mode/alignment'
        assert 0<=pos['opcode']<=(65535 if pos['thumb']else 0xffffffff),'opcode width'
        positions[slot]=pos
    work=collections.Counter();interpreted=[];seen=set();sums=collections.Counter()
    for words in struct.iter_unpack('<8IQ',rows):
        pc,opcode,flags,provenance,guard,admission,status,used,count=words
        if not used:
            assert not any(words),'nonzero unused row';continue
        assert used==1 and count>0 and words[:8]not in seen,'bad or duplicated row';seen.add(words[:8])
        assert flags&~0x11f==0 and provenance<8 and guard<512 and admission<5,'unknown provenance/event'
        assert flags&1 or opcode==0,'unfetched opcode'
        assert not(flags&256)or opcode<=65535,'Thumb width'
        sums['calls']+=count;sums['ok'if status==0 else'nonok']+=count;sums['fetched'if flags&1 else'nofetch']+=count;sums['rows']+=1
        work[(pc,opcode,bool(flags&256),flags&31,status)]+=count
        interpreted.append({'pc':pc,'opcode':opcode,'flags':flags,'provenance':provenance,'guard':guard,'admission':admission,'status':status,'count':count})
    for name in ['calls','ok','nonok','fetched','nofetch','rows']:assert sums[name]==totals[name],name+' conservation'
    assert totals['calls']==totals['ok']+totals['nonok']==totals['fetched']+totals['nofetch']
    gen=[];generated_sum=0
    for slot,(count,)in enumerate(struct.iter_unpack('<Q',generated)):
        if not count:continue
        assert slot in positions,'unmapped generated credit'
        pos=positions[slot];generated_sum+=count;work[(pos['pc'],pos['opcode'],pos['thumb'],1,0)]+=count
        gen.append({**pos,'count':count,'physicalMapping':'unknown; virtual/opcode identity only','routine':'unknown until independently bound'})
    assert generated_sum==int(meta['jitRetiredDelta']),'generated policy conservation'
    assert generated_sum+totals['ok']==int(meta['retiredDelta']),'whole work conservation'
    return {'passed':True,'totals':totals,'generatedRetired':generated_sum,'interpreted':interpreted,'generated':gen,'work':[{'pc':k[0],'opcode':k[1],'thumb':k[2],'events':k[3],'status':k[4],'count':v}for k,v in sorted(work.items())],'scope':'Virtual PC/opcode completed-step footprint; conditional no-ops retire, interpreter exceptions/status remain; no side-effect or physical/routine identity claim.'}
