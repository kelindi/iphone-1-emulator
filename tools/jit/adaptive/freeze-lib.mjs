import {copyFile,mkdir,readFile,lstat} from 'node:fs/promises';
import {constants} from 'node:fs';
import {resolve,dirname,relative,isAbsolute} from 'node:path';
import {createHash} from 'node:crypto';
export const sha256=bytes=>createHash('sha256').update(bytes).digest('hex');
export async function freezeFiles(entries,destination) {
  const root=resolve(destination),rows=[];
  await mkdir(root); // Never overwrite an earlier baseline.
  for(const {source,relative:rel}of entries){
    const target=resolve(root,rel),inside=relative(root,target);
    if(inside.startsWith('..')||isAbsolute(inside)||!inside)throw Error('Invalid freeze destination: '+rel);
    if(!(await lstat(source)).isFile())throw Error('Freeze requires regular files: '+source);
    const hash=sha256(await readFile(source));await mkdir(dirname(target),{recursive:true});
    // A reflink is copy-on-write with its own inode; never use hardlinks.
    await copyFile(source,target,constants.COPYFILE_FICLONE);
    if(sha256(await readFile(target))!==hash)throw Error('Source changed while copying: '+source);
    rows.push({source:resolve(source),relative:rel,sha256:hash,bytes:(await lstat(target)).size});
  }
  for(const row of rows)if(sha256(await readFile(row.source))!==row.sha256)throw Error('Source changed during baseline freeze: '+row.source);
  return rows;
}
export async function verifyFreeze(root,rows){
  for(const row of rows)if(sha256(await readFile(resolve(root,row.relative)))!==row.sha256)throw Error('Frozen file changed: '+row.relative);
  return rows.length;
}
