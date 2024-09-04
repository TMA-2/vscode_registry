import {parentPort, workerData, Worker, WorkerOptions, MessagePort, TransferListItem} from 'worker_threads';
import * as registry from './registry';


class PromiseWorker implements WorkerInterface {
	private worker:		Worker;
	private pending?:	{ resolve: (data: any) => void; reject: (error: any) => void };
	public next?: 		PromiseWorker;

	constructor(script: string, options: WorkerOptions) {
		this.worker = new Worker(script, options);
		this.worker.on('message', (result: any) => {
			this.pending!.resolve(result);
			delete this.pending;
		});
		this.worker.on('error', (result: any) => {
			this.pending!.reject(result);
			delete this.pending;
		});
	}

	post(data: any, transferList?: readonly TransferListItem[]) {
		this.worker.postMessage(data, transferList);
	}
	call<T>(data: any, transferList?: readonly TransferListItem[]): Promise<T> {
		if (this.pending)
			throw new Error("Concurrent call not allowed");
		return new Promise<T>((resolve, reject) => {
			this.pending = { resolve, reject };
			this.worker.postMessage(data, transferList);
		});
	}
}

class WorkerPool implements WorkersInterface {
	workers:	PromiseWorker[];
	free?:		WorkerInterface;
	remaining:	number;
	constructor(script: string, num_workers?: number) {
		if (!num_workers)
			num_workers = +(process.env.NUMBER_OF_PROCESSORS ?? 2);
		this.remaining	= num_workers;
		this.workers	= Array.from({length: num_workers}, ((_, i) => new PromiseWorker(script, {workerData: i})));
		this.free		= this.workers[0];
		this.workers.slice(0, -1).forEach((worker, i) => worker.next = this.workers[i + 1]);
	}
	get(): WorkerInterface|undefined {
		const w = this.free;
		if (w) {
			this.free = w.next;
			--this.remaining;
		}
		return w;
	}
	release(w: WorkerInterface) {
		w.next = this.free;
		this.free = w;
		++this.remaining;
	}
}


export interface Cancellable<T> {
	update: (x: number)=>void;
	found:	(x: T)=>void;
	cancelled: boolean;
}

export interface WorkerInterface {
	call<T>(data: any, transfer?: readonly TransferListItem[]): Promise<T>;
	next?: 	WorkerInterface;
}

export interface WorkersInterface {
	get: () => WorkerInterface | undefined;
	release: (w: WorkerInterface) => void;
}

function test(str: string, pattern: string | RegExp) {
	return typeof pattern === 'string'
		? str.includes(pattern)
		: str.match(pattern);
}


export async function regFindBFS(keypromise: registry.KeyPromise, pattern: string|RegExp, range:number, progress: Cancellable<string>, workers?: WorkersInterface) : Promise<void> {

	const stack: [registry.KeyPromise, number][] = [[keypromise, range]];
	let current: [registry.KeyPromise, number]|undefined;

	while ((current = stack.shift())) {
		range 		= current[1];
		try {
			const key = await current[0];

			if (progress.cancelled)
				return Promise.reject('User cancelled the operation');

			if (test(key.name, pattern))
				progress.found(key.path);

			for (const i in key.values) {
				if (test(i, pattern))
					progress.found(`${key.path}@${i}`);
			}

			const subkeys = Array.from(key);
			if (subkeys.length) {
				range /= (subkeys.length + 1);
				for (const i of key) {
					stack.push([i, range]);
				}
			}
		} catch (error) {
			console.log(error);
		}
		progress.update(range);
	}
}

export async function regFind(key: registry.Key, pattern: string|RegExp, range:number, progress: Cancellable<string>, workers?: WorkersInterface) : Promise<void> {
	if (test(key.name, pattern))
		progress.found(key.path);

	for (const i in key.values) {
		if (test(i, pattern))
			progress.found(`${key.path}@${i}`);
	}

	const subkeys = Array.from(key);
	if (subkeys.length) {
		range /= subkeys.length;
		const promises: Promise<void>[] = [];
		for (const i of key) {
			try {
				if (progress.cancelled)
					return Promise.reject('User cancelled the operation');

				const w = workers?.get();
				if (w) {
					const channel = new MessageChannel;
					channel.port2.on('message', message => {
						if (message.progress)
							progress.update(message.progress);
						if (message.found)
							for (const i of message.found)
								progress.found(i);
					});
	
					promises.push(w.call<string[]>({key: i.path, pattern, range, port: channel.port1}, [channel.port1]).then(
						found => {
							for (const i of found)
								progress.found(i);
							workers!.release(w);
						},
						error => console.log(error)
					));
				} else {
					const k = await i;
					promises.push(regFind(k, pattern, range, progress, workers));
				}

			} catch (error) {
				console.log(error);
			}
		}

		return Promise.all(promises).then(all => undefined)
			.catch(error => Promise.reject(error));
		
	} else {
		progress.update(range);
	}
}


console.log(`Worker started with data: ${workerData}`);

if (parentPort) {
	parentPort.on('message', async data => {
		console.log(`Worker received data: ${data}`);
		if (data === 'exit') {
			process.exit(0);
		}
		
		const port: MessagePort = data.port;

		let 	last_time = performance.now();
		let		accumulated		= 0;
		const 	found: string[] = [];
		const 	progress: Cancellable<string> = {
			update: x => {
				accumulated	+= x;
				const now = performance.now();
				if (now - last_time > 1000) {
					port.postMessage({progress: accumulated, found});
					last_time = now;
					accumulated = 0;
					found.length = 0;
				}
			},
			found:	x => {
				found.push(x);
			},
			cancelled:	false,
		};

		const key = await registry.getKey(data.key);
		await regFind(key, data.pattern, data.range, progress);
		port.postMessage({progress: accumulated});

		parentPort!.postMessage(found);
	});
}
