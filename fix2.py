with open("src/MainWindow.cpp", "r") as f:
    lines = f.readlines()

out = []
i = 0
while i < len(lines):
    if lines[i].strip() == "// Recurse into subfolders" and lines[i+1].strip().startswith("populateChatFolders(folderItem, folder.id, allMessages, db, foldersPtr);"):
        i += 6 # Skip these bogus duplicated lines from the faulty merge
    else:
        out.append(lines[i])
        i += 1

with open("src/MainWindow.cpp", "w") as f:
    f.writelines(out)
