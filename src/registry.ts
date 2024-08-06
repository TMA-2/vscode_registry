import * as path from "path";
import {spawn} from 'child_process';

export const HIVES 		= ['HKLM', 'HKU', 'HKCU', 'HKCR', 'HKCC'];
const HIVES_LONG		= ['HKEY_LOCAL_MACHINE', 'HKEY_USERS', 'HKEY_CURRENT_USER', 'HKEY_CLASSES_ROOT', 'HKEY_CURRENT_CONFIG'];
const KEY_PATTERN   = /(\\[a-zA-Z0-9_\s]+)*/;
const PATH_PATTERN	= /^(HKEY_LOCAL_MACHINE|HKEY_CURRENT_USER|HKEY_CLASSES_ROOT|HKEY_USERS|HKEY_CURRENT_CONFIG).*\\(.*)$/;
const ITEM_PATTERN  = /^(.*)\s(REG_SZ|REG_MULTI_SZ|REG_EXPAND_SZ|REG_DWORD|REG_QWORD|REG_BINARY|REG_NONE)\s+([^\s].*)$/;

const hosts32 : Record<string, KeyImp> = {};
const hosts64 : Record<string, KeyImp> = {};

class Process {
	stdout: string = '';
	stderr: string = '';
	error?: Error;

	constructor(exec: string, args:string[], reject: (reason?: any) => void, close: (proc: Process) => void) {
		console.log(`SPAWN: ${exec} ${args.join(' ')}`);

		const proc = spawn(exec, args, {
			cwd: undefined,
			env: process.env,
			shell: true,
			windowsHide: true,
			stdio: ['ignore', 'pipe', 'pipe']
		});

		proc.stdout.on('data', (data : any) => { this.stdout += data.toString(); });
		proc.stderr.on('data', (data : any) => { this.stderr += data.toString(); });
		proc.on('error', (error: Error) => { this.error = error; });
		proc.on('close', code => {
			if (this.error) {
				reject({
					error: this.error.message,
					code: -1
				});
			} else if (code) {
				const message =`${exec} ${args.join(' ')} command exited with code ${code}:\n${this.stdout.trim()}\n${this.stderr.trim()}`;
				console.log(message);
				reject({
					error: message,
					code: code
				});
			} else {
				close(this);
			}
		});
	}
}

export function string_to_data(type: string, value: string) {
	switch (type) {
		case 'REG_SZ':			return value;
		case 'REG_MULTI_SZ':	return value.split('\\0');
		case 'REG_EXPAND_SZ':	return value;
		case 'REG_DWORD':		return +value;
		case 'REG_QWORD':		return BigInt(value);
		case 'REG_BINARY':		return new Uint8Array(Array.from({length: Math.ceil(value.length / 2)}, (_, i) => value.slice(i * 2, (i + 1) * 2)).map(i => parseInt(i, 16)));
		default:				return;
	}
}

export function data_to_string(value: any) {
	return value.toString();
}

export function value_type(value: any) {
	switch (typeof value) {
		case 'string':		return 'REG_SZ';
		case 'number':		return 'REG_DWORD';
		case 'bigint':		return 'REG_QWORD';
		case 'object':		return Array.isArray(value) ? 'REG_MULTI_SZ' : 'REG_BINARY';
		default:			return 'REG_NONE';
	}
}

function regExec() {
	return process.platform === 'win32' ? path.join(process.env.windir || '', 'system32', 'reg.exe') : "REG";
}

function argName(name?:string) {
	return name ? ['/v', name] : ['/ve'];
}

function argDataString(type: string, value:string) {
	return ['/t', type, ...(type == 'REG_MULTI_SZ' ? ['/s', ','] : []), '/d', `"${value}"`];
}

function argData(value:any) {
	return argDataString(value_type(value), data_to_string(value));
}

class KeyImp {
	public _items?: Promise<Record<string, any>>;
	public _keys: 	Record<string, KeyImp> = {};
	public found?:	boolean;

	private getRoot(): [KeyImp, string] {
		let key = this.name;
		let p 	= this.parent;
		if (p) {
			while (p.parent) {
				key = p.name + '\\' + key;
				p 	= p.parent;
			}
			if (p.name)
				key = `\\\\${p.name}\\${key}`;
			return [p, key];
		}
		return [this, key];
	}
	public getView(root?:KeyImp) {
		if (!root)
			root = this.getRoot()[0];
		return hosts32[root.name] === root ? '32' : '64';
	}

	private runCommand(command:string, ...args:string[]) {
		const [root, fullpath] = this.getRoot();
		const view = hosts32[root.name] === root ? '32' : '64';
		if (view)
			args.push('/reg:' + view);

		return new Promise<Process>((resolve, reject) => new Process(regExec(), [command, `"${fullpath}"`, ...args], reject, resolve));
	}

	private add_found_key(key:string) {
		if (key && key !== this.name) {
			if (!(key in this._keys))
				this._keys[key] = new KeyImp(key, this);
			this._keys[key].found = true;
		}
	}

	public reread() : Promise<Record<string, any>> {
		return this._items = this.runCommand('QUERY').then(proc => {
			const items : Record<string, any> = {};
			let lineNumber = 0;
			for (const i of proc.stdout.split('\n')) {
				const line = i.trim();
				if (line.length > 0) {
					if (lineNumber++ !== 0) {
						const match = ITEM_PATTERN.exec(line);
						if (match) {
							items[match[1].trim()] = string_to_data(match[2].trim(), match[3]);
							continue;
						}
					}

					const match = PATH_PATTERN.exec(line);
					if (match)
						this.add_found_key(match[2]);
				}
			}
			for (let p : KeyImp = this; !p.found && p.parent; p = p.parent)
				p.found = true;
			return items;
		});
	}

	public read() : Promise<Record<string, any>> {
		return this._items ?? this.reread();
	}

	constructor(public name: string, public parent?: KeyImp) {}

	public toString() {
		return this.getRoot()[1];
	}

	public subkey(key: string) : KeyImp {
		let p: KeyImp = this;
		for (const i of key.split('\\')) {
			if (!p._keys[i])
				p._keys[i] = new KeyImp(i, p);
			p = p._keys[i];
		}
		return p;
	}

	public async exists() : Promise<boolean> {
		return this.found || (!this.parent?.found && await this.read().then(() => true, () => false));
	}

	public async clear() : Promise<boolean> {
		if (this._items) {
			this._items.then(x => {
				for (const i in x)
					delete x[i];
			});
		}
		return this.runCommand('DELETE', '/f', '/va').then(() => true, () => false);
	}

	public async destroy() : Promise<boolean> {
		return this.runCommand('DELETE', '/f').then(
			() => { delete this.parent?._keys[this.name]; return true; },
			() => false
		);
	}

	public async create() : Promise<Key|undefined> {
		return this.runCommand('ADD', '/f').then(() => MakeKey(this), () => undefined);
	}

	public async deleteValue(key: string) : Promise<boolean> {
		if (this._items)
			this._items.then(x => delete x[key]);
		return this.runCommand('DELETE', ...argName(key), '/f').then(() => true, () => false);
	}

	public async setValue(key: string, value: any) : Promise<boolean> {
		return this.runCommand('ADD', ...argName(key), ...argData(value), '/f').then(
			() => this._items
				? this._items.then(x => { x[key] = value; return true; })
				: true,
			() => false
		);
	}

	public async setValueString(key: string, type: string, value: string) : Promise<boolean> {
		return this.runCommand('ADD', ...argName(key), ...argDataString(type, value), '/f').then(
			() => this._items
				? this._items.then(x => { x[key] = string_to_data(type, value); return true; })
				: true,
			() => false
		);
	}

	public async export(file: string) : Promise<boolean> {
		return this.runCommand('EXPORT', file, '/y').then(() => true, () => false);
	}

	*[Symbol.iterator]() {
		for (const k in this._keys)
			yield MakeKey(this._keys[k]);
	}
}

interface Values {
	[key:string]:any;
	clear: ()=>void;
	then: (func: (x: Record<string, any>)=>void)=>unknown;
}

export interface KeyBase {
	name:		string;
	parent:		KeyImp;
	exists:		() => Promise<boolean>;
	clear:		() => Promise<boolean>;
	destroy:	() => Promise<boolean>;
	create:		() => Promise<Key|undefined>;
	deleteValue:(key: string) 				=> Promise<boolean>;
	setValue:	(key: string, value: any)	=> Promise<boolean>;
	export:		(file: string) 				=> Promise<boolean>;
	toString:	() => string;
	values: 	Values;
}

export interface Key extends KeyBase {
	[key:string|symbol]:any;
	[Symbol.iterator]: () => any;
}

function MakeValues(p: KeyImp) : Values {
	return new Proxy(p as unknown as Values, {
		get: (obj, key: string) => {
			if (key == 'then')
				return p.read().then.bind(p._items);
			return p.read().then(x => x[key]);
		},
		set: (obj, key:string, value) => {
			p.setValue(key, value);
			return true;
		},
		//has: (obj, key: string) => {
		//	return key in p._keys;
		//},
		deleteProperty: (obj, key: string) => {
			p.deleteValue(key);
			return true;
		}
	});
}

function MakeKey(p: KeyImp): Key {
	return new Proxy(p as unknown as Key, {
		get: (obj, key: string | symbol, receiver) => {
			const v = p[key as keyof KeyImp];
			if (v)
				return typeof v === 'function' ? v.bind(p) : v;
	
			if (typeof key === 'string') {
				switch (key) {
					case 'values':
						return MakeValues(p);
					case 'then': {
						const a = p.read().then(() => p);
						return a.then.bind(a);
					}
					default:
						return MakeKey(p.subkey(key));
				}
			}
		},
		has: (obj, key: string) => {
			return key in p._keys;
		},
		deleteProperty: (obj, key: string) => {
			p.subkey(key).destroy();
			return true;
		},
	});
}

export function getRawKey(key:string, view?:string) : KeyImp {
	let host = '';
	if (key.startsWith('\\\\')) {
		const i = key.indexOf('\\', 2);
		host	= key.substring(2, i);
		key		= key.substring(i + 1);
	}
	
	let i = key.indexOf('\\');
	if (i === -1)
		i = key.length;

	let hive_index = HIVES.indexOf(key.substring(0, i));
	if (hive_index === -1) {
		hive_index = HIVES_LONG.indexOf(key.substring(0, i));
		if (hive_index === -1)
			throw new Error('illegal hive specified.');
		key = `${HIVES[hive_index]}${key.substring(i)}`;
	}

	if (host && hive_index >= 2)
		throw new Error('For remote access the root key must be HKLM or HKU');

	if (!KEY_PATTERN.test(key ?? ''))
		throw new Error('illegal key specified.');

	if (view && view != '32' && view != '64')
		throw new Error('illegal view specified (use 32 or 64)');

	const hosts = view == '32' ? hosts32 : hosts64;
	let p = hosts[host];
	if (!p)
		hosts[host] = p = new KeyImp(host);

	return p.subkey(key);
}

export function getKey(key:string, view?:string): Key {
	return MakeKey(getRawKey(key, view));
}

export async function importreg(file: string, view?: string, dirty?: KeyBase[]) : Promise<boolean> {
	const args = ['IMPORT', file];
	if (view)
		args.push('/reg:' + view);

	return new Promise<Process>((resolve, reject) => new Process(regExec(), args, reject, resolve))
		.then(() => {
			if (dirty) {
				const parents = new Set<KeyImp>();
				for (const i of dirty) {
					const parent = i.parent;
					parents.add(parent);
					delete parent._keys[i.name];
				}
				Promise.all(Array.from(parents).map(p => p.reread())).then(() => true);

			} else {
				const hosts = view === '32' ? hosts32 : hosts64;
				for (const i in hosts)
					delete hosts[i];
			}
			return true;
		},
		() => false
	);
}

export async function test() {
	return;
//	const key = getKey('HKCU');
//	const xyz = key.xyz.create();
//	const xyz2 = await xyz;
//	key.xyz.values.abc = 'hello';

	const key = getKey('HKCU');
	if (await key.exists()) {
		for (const i of key) {
			console.log(i.toString());
			for (const [k,v] of Object.entries(await i.values))
				console.log(`${k} = ${v}`);
		}

		const value = await key.values.ServiceLastKnownStatus;
		if (value)
			console.log(value.data);
	}
}