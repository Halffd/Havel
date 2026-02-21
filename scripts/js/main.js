// Havel Compositor Bridge for KWin
// Writes active window info to a file for external apps

const outputPath = `${Qt.platform.os === 'linux' ? process.env.HOME : ''
    }/.local/share/kwin/scripts/havelbridge/activewindow.txt`;

function updateActiveWindow() {
    const client = workspace.activeClient;
    if (!client) return;
    
    const data = `title=${client.caption}
appid=${client.resourceClass}
pid=${client.pid}
`;
    
    // Write to file
    const file = new TextFile(outputPath, TextFile.WriteOnly);
    file.write(data);
    file.commit();
    file.close();
}

// Update on active window change
workspace.clientActivated.connect(updateActiveWindow);

// Initial update
updateActiveWindow();