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
	return vscode.workspace.fs.readFile(vscode.Uri.file(file)).then(
		bytes => new TextDecoder(BOMtoEncoding(bytes)).decode(bytes),
		error => console.log(`Failed to load ${file} : ${error}`)
	);
}

//-----------------------------------------------------------------------------
//	Tree View
//-----------------------------------------------------------------------------

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

	public get label_text() {
		return typeof(this.label) == 'string' ? this.label : this.label?.label;
	}

	public collapse(): void {
		if (this.collapsibleState !== TreeItemCollapsibleState.None) {
			if (this.children) 
				this.children.forEach(c => c.collapse());
			this.collapsibleState = TreeItemCollapsibleState.Collapsed;
		}
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
    constructor(parent : TreeItem, public name: string, public data: any) {
		super(parent, `${name}:${registry.value_type(data)} = ${data}`, TreeItemCollapsibleState.None);
		this.contextValue = 'value';
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

				await vscode.workspace.fs.writeFile(vscode.Uri.file(file),  Buffer.from(text, 'utf8'));
				await registry.importreg(file, key.getView(), [key]);
				await vscode.workspace.fs.delete(vscode.Uri.file(file));

				return true;
			}
		}
	}
	return false;
}

export class RegEditProvider implements vscode.TreeDataProvider<TreeItem> {
	private treeView: 		vscode.TreeView<TreeItem>;
	private children: 		TreeItem[]	= [];

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
	public async collapse(node: TreeItem) {
		node.collapsibleState = TreeItemCollapsibleState.Collapsed;
		this.recreate(node);
	}

	public getTreeItem(element: TreeItem): vscode.TreeItem {
		return element;
	}

	public getRootByContext(element: TreeItem, type : string) {
		let i : TreeItem | null = element;
		while (i && !(i.contextValue != type))
			i = i.parent;
		return i;
	}

	public getRootByClass(element: TreeItem, type: any) {
		let i : TreeItem | null = element;
		while (i && !(i instanceof type))
			i = i.parent;
		return i;
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

	public getSelectedItems(): readonly TreeItem[] {
		return this.treeView.selection;
	}

	constructor() {
		const options = {
			treeDataProvider: this,
			canSelectMany: true,
			showCollapseAll: true
		};

		this.treeView = vscode.window.createTreeView('regedit-view', options);

	}
}

//-----------------------------------------------------------------------------
//	File System Stub
//-----------------------------------------------------------------------------

class RegFS implements vscode.FileSystemProvider {
	private _onDidChangeFile = new vscode.EventEmitter<vscode.FileChangeEvent[]>();
	readonly onDidChangeFile: vscode.Event<vscode.FileChangeEvent[]> = this._onDidChangeFile.event;

	constructor(public view: RegEditProvider) {}

	async stat(uri: vscode.Uri): Promise<vscode.FileStat> {
		return {
			type: vscode.FileType.File,
			ctime: Date.now(),
			mtime: Date.now(),
			size: 0,
		};
	}

	async readFile(uri: vscode.Uri): Promise<Uint8Array> {
		const file = path.join(os.tmpdir(), 'temp.reg');
		return await registry.getKey(uri.path.substring(1)).export(file).then(() => vscode.workspace.fs.readFile(vscode.Uri.file(file)));
	}

	async writeFile(uri: vscode.Uri, content: Uint8Array, options: { create: boolean, overwrite: boolean }): Promise<void> {
		const file = path.join(os.tmpdir(), 'temp.reg');
		await vscode.workspace.fs.writeFile(vscode.Uri.file(file),content);
		registry.importreg(file).then(() => this.view.recreate());
	}

	//stubs
	readDirectory(uri: vscode.Uri): [string, vscode.FileType][] { return []; }
	createDirectory(uri: vscode.Uri): void {}
	delete(uri: vscode.Uri): void {}
	rename(oldUri: vscode.Uri, newUri: vscode.Uri, options: { overwrite: boolean }): void {}
	watch(uri: vscode.Uri): vscode.Disposable { return new vscode.Disposable(() => {}); }

}

//-----------------------------------------------------------------------------
//	main extension entry point
//-----------------------------------------------------------------------------

let _regedit: RegEditProvider|undefined;

export function activate(context: vscode.ExtensionContext) {
	function registerCommand(command: string, callback: (...args: any[]) => any, thisArg?: any) {
		const disposable = vscode.commands.registerCommand(command, callback);
		context.subscriptions.push(disposable);
	}

	registerCommand('regedit.open', () => {
		if (!_regedit) {
			vscode.commands.executeCommand('setContext', 'regedit.open', true);
			_regedit = new RegEditProvider;
		}
		const regedit = _regedit;

		vscode.workspace.registerFileSystemProvider('reg', new RegFS(regedit), { isCaseSensitive: true });
			
		registerCommand("regedit.createKey",	async (item?: TreeItem, name?: string) => {
			if (!item)
				item = regedit.getSelectedItems()[0];
			if (item instanceof KeyTreeItem) {
				if (!name) {
					name = await vscode.window.showInputBox({prompt: 'Enter the name of the new Key'});
					if (!name)
						return;
				}

				if (await item.key[name].create())
					regedit.recreate(item);
			}
		});

		registerCommand("regedit.createValue",	async (item?: TreeItem, name?: string, type?: string, data?:string) => {
			if (!item)
				item = regedit.getSelectedItems()[0];
			if (item instanceof KeyTreeItem) {
				if (!name) {
					name = await vscode.window.showInputBox({prompt: 'Enter the name of the new Value'});
					if (!name)
						return;
				}

				if (!type) {
					const items: vscode.QuickPickItem[] = [
						{ label: 'REG_SZ'		},
						{ label: 'REG_MULTI_SZ'	},
						{ label: 'REG_EXPAND_SZ'},
						{ label: 'REG_DWORD'	},
						{ label: 'REG_QWORD'	},
						{ label: 'REG_BINARY'	},
						{ label: 'REG_NONE'		},
					];
					
					const selectedItem = await vscode.window.showQuickPick(items, {placeHolder: 'Select the value type'});
					if (!selectedItem)
						return;
					type = selectedItem.label;
				}

				if (!data) {
					switch (type) {
						case 'REG_EXPAND_SZ':
						case 'REG_SZ':			data = '<new>'; break;
						case 'REG_MULTI_SZ':	data = '<new>;<new>'; break;
						case 'REG_DWORD':
						case 'REG_QWORD':
						case 'REG_BINARY':		data = '0'; break;
						default:				break;
					}

					data = await vscode.window.showInputBox({prompt: 'Enter the initial value', value: data});
					if (!data)
						return;
				}

				if (await item.key.setValue(name, registry.string_to_data(type, data)))
					regedit.recreate(item);
			}
		});

		registerCommand("regedit.editValue",	async (item?: TreeItem, data?: string) => {
			if (!item)
				item = regedit.getSelectedItems()[0];
			if (item instanceof ValueTreeItem) {
				const old_data = item.data;
				if (!data) {
					data = await vscode.window.showInputBox({prompt: 'Enter the new value', value: registry.data_to_string(old_data)});
					if (!data)
						return;
				}
				const parent	= item.parent as KeyTreeItem;
				if (await parent.key.setValue(item.name, registry.string_to_data(registry.value_type(old_data), data)))
					regedit.recreate(parent);
			}
		});


		registerCommand("regedit.delete",			async (item?: TreeItem) => {
			if (!item) {
				item = regedit.getSelectedItems()[0];
			} else if (await vscode.window.showInformationMessage('Are you sure you want to delete ?', { modal: true }, 'Yes', 'No') !== 'Yes') {
				return;
			}

			const parent	= item.parent as KeyTreeItem;

			if (item instanceof KeyTreeItem) {
				if (await item.key.destroy())
					regedit.recreate(parent);

			} else if (item instanceof ValueTreeItem) {
				if (await parent.key.deleteValue(item.name))
					regedit.recreate(parent);
			}
		});

		registerCommand("regedit.rename",			async (item?: TreeItem) => {
			if (!item)
				item = regedit.getSelectedItems()[0];

			if (item instanceof KeyTreeItem) {
				const newName = await vscode.window.showInputBox({
					value: item.label as string,
					prompt: 'Enter the new name',
				});
				if (newName) {
					await rename(item.key, newName).then(() => {
						const parent	= item.parent as KeyTreeItem;
						regedit.recreate(parent);
					});		
				}

			} else if (item instanceof ValueTreeItem) {
				const newName = await vscode.window.showInputBox({
					value: item.name as string,
					prompt: 'Enter the new name',
				});
				if (newName) {
					const parent	= item.parent as KeyTreeItem;
					await rename(parent.key, newName, item.name).then(() => {
						regedit.recreate(parent);
					});
				}
			}
		});
		registerCommand("regedit.export",			async (item?: TreeItem) => {
			if (!item)
				item = regedit.getSelectedItems()[0];

			if (item instanceof KeyTreeItem) {
				const uri = vscode.Uri.parse(`reg:/${item.key}`);
				const document = await vscode.workspace.openTextDocument(uri);

				// Show the document in the active editor column
				await vscode.window.showTextDocument(document, {viewColumn: vscode.ViewColumn.Active});
			}
		});

		registerCommand("regedit.import",			async (item?: any) => {
			const editor = vscode.window.activeTextEditor;
			if (editor) {
				const document = editor.document;
				const text = document.getText();
				console.log(text);
				document.save();

				const file = path.join(os.tmpdir(), 'temp.reg');
				await vscode.workspace.fs.writeFile(vscode.Uri.file(file), Buffer.from(text, 'utf8'));
				registry.importreg(file).then(() => regedit.recreate());
		
			}
		});
	});
}
