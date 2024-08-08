import * as os from "os";
import * as path from "path";
import * as vscode from "vscode";
import * as registry from "./registry";

import {TreeItemCollapsibleState, Uri} from "vscode";

//-----------------------------------------------------------------------------
//	helpers
//-----------------------------------------------------------------------------

function compare<T>(a: T, b: T) : number {
	return a < b ? -1 : a > b ? 1 : 0;
}

function BOMtoEncoding(bytes: Uint8Array): string {
	return	bytes.length >= 3 && bytes[0] === 0xEF && bytes[1] === 0xBB && bytes[2] === 0xBF ?'utf-8'
		:	bytes.length >= 2 && bytes[0] === 0xFE && bytes[1] === 0xFF ? 'utf-16be'
		:	bytes.length >= 2 && bytes[0] === 0xFF && bytes[1] === 0xFE ? 'utf-16le'
		: 	'utf-8';
}

function loadTextFile(file: string): Thenable<string> {
	return vscode.workspace.fs.readFile(Uri.file(file)).then(
		bytes => new TextDecoder(BOMtoEncoding(bytes)).decode(bytes),
		error => console.log(`Failed to load ${file} : ${error}`)
	);
}

async function rename(key: registry.Key, newName: string, value?: string) : Promise<boolean> {
	if (value) {
		const values = await key.values;
		if ((value in values) && !(newName in values)) {
			const data = values[value];
			await key.setValue(newName, data);
			delete values[value];
			return true;
		}

	} else if (key.parent && !(newName in key.parent)) {
		const file = path.join(os.tmpdir(), 'temp.reg');
		await key.export(file);
		let text = await loadTextFile(file);
		const re = /\[.*\\(.*)\]/d;
		const m = re.exec(text);
		if (m && m.indices) {
			const a = m.indices[1][0], b = m.indices[1][1];
			if (text.substring(a, b) == key.name) {
				text = text.slice(0, a) + newName + text.slice(b) + '[-' + text.slice(m.indices[0][0] + 1, b + 2);

				await vscode.workspace.fs.writeFile(Uri.file(file),  Buffer.from(text, 'utf8'));
				await registry.importreg(file, key.getView(), [key]);
				await vscode.workspace.fs.delete(Uri.file(file));

				return true;
			}
		}
	}
	return false;
}


//-----------------------------------------------------------------------------
//	Tree View
//-----------------------------------------------------------------------------

type TypeDecoration = {
	icon:	string,
	color: string,
	badge: string,
}

const decorations : Record<string, TypeDecoration> = {
	'SZ':			{ icon: 'symbol-text', 		color: 'charts.red', 	badge: 'SZ'},
	'MULTI_SZ':		{ icon: 'symbol-array', 	color: 'charts.blue', 	badge: 'MS'},
	'EXPAND_SZ':	{ icon: 'symbol-text', 		color: 'charts.yellow', badge: 'ES'},
	'DWORD':		{ icon: 'symbol-number', 	color: 'charts.orange', badge: 'DW'},
	'QWORD':		{ icon: 'symbol-number', 	color: 'charts.green', 	badge: 'QW'},
	'BINARY':		{ icon: 'file-binary', 		color: 'charts.purple', badge: 'BI'},
};

abstract class TreeItem extends vscode.TreeItem {
	public children: TreeItem[] | null = null;

	constructor(
		public parent: TreeItem | null,
		label: string | vscode.TreeItemLabel,
		collapsibleState: TreeItemCollapsibleState = TreeItemCollapsibleState.None,
		path?: string,
	) {
		super(label, collapsibleState);
		if (path)
			this.resourceUri = Uri.file(path);
	}

	public clearChildren(): void {
		if (this.children)
			this.children.forEach(c => c.clearChildren());
		this.children = null;
	}

	public createChildren(): Promise<TreeItem[]> {
		return Promise.resolve([]);
	}
}

class ValueTreeItem extends TreeItem {
    constructor(parent : KeyTreeItem, public name: string, public data: registry.Data) {
		super(parent, `${name} = ${registry.data_to_regstring(data)}`, TreeItemCollapsibleState.None);
		const type 			= data.constructor.name;
		const decoration	= decorations[type];
		this.contextValue 	= 'value';
		this.iconPath		= new vscode.ThemeIcon(decoration.icon, new vscode.ThemeColor(decoration.color));
		this.resourceUri	= Uri.parse(`reg:/${parent.key.path.replace(/\\/g,'/')}/${name}?type=${type}`);
	}
}

class KeyTreeItem extends TreeItem {
    constructor(parent : TreeItem | null, public key: registry.Key) {
		super(parent, key.name, TreeItemCollapsibleState.Collapsed);
		this.contextValue = 'key';
	}
	async createChildren(): Promise<TreeItem[]> {
		const children : TreeItem[] = [];
		for (const i of (Array.from(await this.key) as registry.Key[]).sort((a, b) => compare(a.name, b.name)))
			children.push(new KeyTreeItem(this, i));
		
		for (const [k, v] of Object.entries(await this.key.values))
			children.push(new ValueTreeItem(this, k, v));

		return children;
    }
}

export class RegEditProvider implements vscode.TreeDataProvider<TreeItem>, vscode.FileDecorationProvider {
	//private treeView: 		vscode.TreeView<TreeItem>;
	private children: 	TreeItem[]	= [];
	public	selection:	TreeItem[] = [];

	private _onDidChangeTreeData = new vscode.EventEmitter<TreeItem | undefined | null | void>();
	readonly onDidChangeTreeData = this._onDidChangeTreeData.event;

	public refresh(node?: TreeItem): void {
		this._onDidChangeTreeData.fire(node);
	}
	public recreate(node?: TreeItem): void {
		if (node) {
			node.clearChildren();
		} else {
			this.children.forEach(c => c.clearChildren());
			this.children = [];
		}
		this._onDidChangeTreeData.fire(node);
	}

	public getTreeItem(element: TreeItem): vscode.TreeItem {
		return element;
	}

	public getChildren(element?: TreeItem) {//}: Promise<TreeItem[]> | undefined {
		if (element) {
			if (!element.children) {
				return element.createChildren().then(children => {
					return element.children = children;
				}).catch(err => {
					console.log(`get children: ${element.label} err=${err}`);
					return element.children = [];
				});
			}
			return Promise.resolve(element.children);
		}

		if (!this.children.length) {
			for (const h of registry.HIVES)
				this.children.push(new KeyTreeItem(null, registry.getKey(h)));
		}
		return Promise.resolve(this.children);
	}

	public getParent(element: TreeItem): TreeItem | null {
		return element.parent;
	}

	public getKeyItem(key: registry.Key): KeyTreeItem | undefined {
		let node: KeyTreeItem | undefined;
		for (const i of key.path.split('\\')) {
			const children: TreeItem[] | null = node ? node.children : this.children;
			if (!children || !(node = children.find(j => (j instanceof KeyTreeItem) && j.label == i) as KeyTreeItem | undefined))
				return;
		}
		return node;
	}

	provideFileDecoration(uri: Uri, token: vscode.CancellationToken): vscode.ProviderResult<vscode.FileDecoration> {
		if (uri.scheme === 'reg') {
			const decoration = decorations[uri.query.substring(5)];
			return {
				badge: decoration.badge,
				//color: new vscode.ThemeColor(decoration.color), 
				// tooltip: ""
			};
		}
		return null;  // to get rid of the custom fileDecoration
    }

	constructor() {
		vscode.window.registerTreeDataProvider('regedit-view', this);
		vscode.workspace.registerFileSystemProvider('reg', new RegFS(this), { isCaseSensitive: true });
		vscode.window.registerFileDecorationProvider(this);
	}
}

//-----------------------------------------------------------------------------
//	File System Stub
//-----------------------------------------------------------------------------

class RegFS implements vscode.FileSystemProvider {
	private _onDidChangeFile = new vscode.EventEmitter<vscode.FileChangeEvent[]>();
	readonly onDidChangeFile: vscode.Event<vscode.FileChangeEvent[]> = this._onDidChangeFile.event;

	constructor(public view: RegEditProvider) {}

	async stat(uri: Uri): Promise<vscode.FileStat> {
		return {
			type: vscode.FileType.File,
			ctime: Date.now(),
			mtime: Date.now(),
			size: 0,
		};
	}

	async readFile(uri: Uri): Promise<Uint8Array> {
		console.log(`read ${uri}`);
		const file = path.join(os.tmpdir(), 'temp.reg');
		return await registry.getKey(uri.path.substring(1)).export(file).then(() => vscode.workspace.fs.readFile(Uri.file(file)));
	}

	async writeFile(uri: Uri, content: Uint8Array, options: { create: boolean, overwrite: boolean }): Promise<void> {
		const file = path.join(os.tmpdir(), 'temp.reg');
		await vscode.workspace.fs.writeFile(Uri.file(file),content);
		registry.importreg(file).then(() => {
			this.view.recreate();
			this._onDidChangeFile.fire([{ type: vscode.FileChangeType.Changed, uri }]);
		});
	}

	private _bufferedEvents: vscode.FileChangeEvent[] = [];

	private fireEvents(events: vscode.FileChangeEvent[]): void {
		this._onDidChangeFile.fire(events);
	}

	//stubs
	readDirectory(uri: Uri): [string, vscode.FileType][] { return []; }
	createDirectory(uri: Uri): void {}
	delete(uri: Uri): void {}
	rename(oldUri: Uri, newUri: Uri, options: { overwrite: boolean }): void {}
	watch(uri: Uri): vscode.Disposable { return new vscode.Disposable(() => {}); }

}

//-----------------------------------------------------------------------------
//	main extension entry point
//-----------------------------------------------------------------------------

function copy(item: TreeItem, strict: boolean) {
	if (item instanceof KeyTreeItem) {
		const key = item.key;
		vscode.env.clipboard.writeText(`[${key.path}]`);

	} else if (item instanceof ValueTreeItem) {
		const key = (item.parent as KeyTreeItem).key;
		vscode.env.clipboard.writeText(`[${key.path}]\n"${item.name}"=${registry.data_to_regstring(item.data, strict)}`);
	}
}

export function activate(context: vscode.ExtensionContext) {
	
	function registerCommand(command: string, callback: (...args: any[]) => any, thisArg?: any) {
		const disposable = vscode.commands.registerCommand(command, callback);
		context.subscriptions.push(disposable);
	}

	const regedit = new RegEditProvider;

	registerCommand("regedit.edit",	async (item?: TreeItem, data?: string) => {
		if (item instanceof ValueTreeItem) {
			const old_data = item.data;
			if (!data) {
				data = await vscode.window.showInputBox({prompt: 'Enter the new value', value: old_data.toString()});
				if (!data)
					return;
			}
			const parent	= item.parent as KeyTreeItem;
			const data2		= old_data.constructor as registry.Type;
			if (await parent.key.setValue(item.name, data2.parse(data)))
				regedit?.recreate(parent);

		} else if (item instanceof KeyTreeItem) {
			const document = await vscode.workspace.openTextDocument(Uri.parse(`reg:/${item.key}`));
			await vscode.window.showTextDocument(document, {viewColumn: vscode.ViewColumn.Active});
		}
	});
		
	registerCommand("regedit.createKey",	async (item: string|TreeItem, name?: string) => {
		if (typeof(item) == 'string')
			return registry.getKey(item).create();

		if (item instanceof KeyTreeItem) {
			if (!name) {
				name = await vscode.window.showInputBox({prompt: 'Enter the name of the new Key'});
				if (!name)
					return;
			}

			if (await item.key[name].create())
				regedit?.recreate(item);
		}
	});

	registerCommand("regedit.setValue",	async (item: string|TreeItem, name?: string, stype?: string, data?:string) => {
		if (typeof(item) == 'string') {
			const type = stype ? registry.string_to_type(stype) : registry.TYPES.SZ;
			if (type)
				registry.getKey(item).setValue(item, type.parse(data || ''));
			return;
		}

		if (item instanceof KeyTreeItem) {
			if (!name) {
				name = await vscode.window.showInputBox({prompt: 'Enter the name of the new Value'});
				if (!name)
					return;
			}

			if (!stype) {
				const items = [
					{ label: 'REG_SZ'			},
					{ label: 'REG_MULTI_SZ'		},
					{ label: 'REG_EXPAND_SZ'	},
					{ label: 'REG_DWORD'		},
					{ label: 'REG_QWORD'		},
					{ label: 'REG_BINARY'		},
					{ label: 'REG_NONE'			},
				];
				
				const selectedItem = await vscode.window.showQuickPick(items, {placeHolder: 'Select the value type'});
				if (!selectedItem)
					return;
				stype = selectedItem.label;
			}

			if (!data) {
				switch (stype) {
					case 'REG_EXPAND_SZ':
					case 'REG_SZ':			data = '<new>'; break;
					case 'REG_MULTI_SZ':	data = '<new>\\0<new>'; break;
					case 'REG_DWORD':
					case 'REG_QWORD':
					case 'REG_BINARY':		data = '0'; break;
					default:				break;
				}

				data = await vscode.window.showInputBox({prompt: 'Enter the initial value', value: data});
				if (!data)
					return;
			}

			const type = registry.string_to_type(stype);
			if (type && await item.key.setValue(name, type.parse(data)))
				regedit.recreate(item);
		}
	});

	registerCommand("regedit.delete",			async (item: string|TreeItem) => {
		if (typeof(item) == 'string') {
			const key = await registry.getKey(path.dirname(item));
			const base = path.basename(item);
			if (base in key)
				delete key[base];
			else
				key.deleteValue(base);
			return;
		}

		if (await vscode.window.showInformationMessage('Are you sure you want to delete ?', { modal: true }, 'Yes', 'No') !== 'Yes')
			return;

		if (item instanceof KeyTreeItem) {
			const parent	= item.parent as KeyTreeItem;
			if (await item.key.destroy())
				regedit.recreate(parent);

		} else if (item instanceof ValueTreeItem) {
			const parent	= item.parent as KeyTreeItem;
			if (await parent.key.deleteValue(item.name))
				regedit.recreate(parent);
		}
	});

	registerCommand("regedit.rename",			async (item: string|TreeItem, newname?: string) => {
		let key:registry.Key;
		let name:string;
		let value:string = '';
		let parent:KeyTreeItem | undefined;

		if (typeof(item) == 'string') {
			key		= await registry.getKey(path.dirname(item));
			name	= path.basename(item);
			if (name in key) {
				key		= key[value];
				value	= '';
			} else {
				value	= name;
			}
			parent = regedit?.getKeyItem(key);

		} else {
			parent	= item?.parent as KeyTreeItem;
			if (item instanceof KeyTreeItem) {
				key 	= item.key;
				name 	= key.name;
			} else if (item instanceof ValueTreeItem) {
				key 	= parent.key;
				name	= item.name as string;
				value	= name;
			} else {
				return;
			}
		}

		if (!newname) {
			newname = await vscode.window.showInputBox({
				value: name,
				prompt: 'Enter the new name',
			});
			if (!newname)
				return;
		}

		await rename(key, newname, value).then(() => regedit?.recreate(parent));		
	});

	registerCommand("regedit.export",			async (item: string|TreeItem, file?: string) => {
		const key	= typeof(item) == 'string'		? registry.getKey(item)
					: item instanceof KeyTreeItem	? item.key
					: undefined;
		if (key) {
			if (!file) {
				const options: vscode.OpenDialogOptions = {
					canSelectMany: false, // Set to true if you want to allow multiple file selection
					filters: {
						'Registry Files': ['reg'],
						'All Files': ['*']
					}
				};
		
				const uris = await vscode.window.showOpenDialog(options);
				if (uris && uris.length)
					file = uris[0].fsPath;
			}

			if (file)
				return await key.export(file);
		}

	});

	registerCommand("regedit.import",			async (file: string) => {
		registry.importreg(file).then(() => regedit?.recreate());
	});
	registerCommand("regedit.copy",				async (item: TreeItem) => {
		copy(item, false);
	});
	registerCommand("regedit.copy_strict",		async (item: TreeItem) => {
		copy(item, true);
	});
}
