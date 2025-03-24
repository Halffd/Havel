# Handling Large Files in the HvC Repository

This document provides guidelines for handling large files in the HvC repository to prevent issues with GitHub's file size limits.

## GitHub File Size Limitations

- GitHub has a file size limit of 100 MB for individual files
- Files larger than 50 MB will trigger a warning

## What Files to Avoid Committing

The following types of files should **never** be committed to the repository:

1. **Precompiled headers** (`.gch`, `.pch`)
2. **Compiled binaries** (`.o`, `.a`, `.so`, `.exe`, `.dll`)
3. **Generated build files** (all files in the `build/` directory)
4. **Temporary files** (`.tmp`, `.bak`, etc.)
5. **Large media files** (videos, high-resolution images, etc.)

## Using Git LFS for Large Files

If you need to track large files (such as documentation assets, datasets, etc.), consider using Git Large File Storage (Git LFS):

1. Install Git LFS:
   ```bash
   # Ubuntu/Debian
   sudo apt install git-lfs
   
   # Arch Linux
   sudo pacman -S git-lfs
   
   # macOS
   brew install git-lfs
   ```

2. Set up Git LFS in your repository:
   ```bash
   git lfs install
   ```

3. Track specific file patterns:
   ```bash
   git lfs track "*.pdf"
   git lfs track "*.png"
   git lfs track "*.mp4"
   ```

4. Make sure `.gitattributes` is tracked:
   ```bash
   git add .gitattributes
   ```

5. Use Git normally from now on:
   ```bash
   git add my-large-file.pdf
   git commit -m "Add large PDF document"
   git push
   ```

## Handling Accidental Commits of Large Files

If you accidentally commit a large file, follow these steps to remove it:

1. Add the file pattern to `.gitignore`:
   ```bash
   echo "*.gch" >> .gitignore
   git add .gitignore
   git commit -m "Add *.gch to gitignore"
   ```

2. Remove the file from Git history:
   ```bash
   git filter-branch --force --index-filter \
     "git rm --cached --ignore-unmatch path/to/large-file" \
     HEAD
   ```

3. Force push the changes:
   ```bash
   git push origin --force
   ```

## Best Practices

- Always check your commits for large files before pushing
- Set up a pre-commit hook to reject large files
- Use `.gitignore` to prevent accidentally committing unwanted files
- Consider using Git LFS from the start if your project requires large files 