import * as path from 'path';
import { workspace, ExtensionContext } from 'vscode';

import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: ExtensionContext) {
    // The server is implemented as a command line executable
    const serverCommand: string = workspace.getConfiguration('havel.languageServer').get('path') || 'havel-lsp';
    
    const serverOptions: ServerOptions = {
        command: serverCommand,
        transport: TransportKind.stdio
    };

    // Options to control the language client
    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'havel' }],
        synchronize: {
            configurationSection: 'havel',
            fileEvents: workspace.createFileSystemWatcher('**/*.hv')
        }
    };

    // Create the language client and start the client
    client = new LanguageClient(
        'havelLanguageServer',
        'Havel Language Server',
        serverOptions,
        clientOptions
    );

    // Start the client. This will also launch the server
    client.start();
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
