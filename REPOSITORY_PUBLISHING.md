# Publishing this prepared repository to GitHub

Suggested hosted repository: **`vjmichael/single-galaxy`** — **Private** until licensing/provenance and desired visibility are decided.

This package is an **already initialized local Git repository**. A remote GitHub repository was not created automatically because the connected GitHub actions available at packaging time could not create a new repository.

## GitHub Desktop (no terminal required)

1. Extract the supplied `SingleGalaxy_Git_Repository_Starter_2026-10-31.zip` to a stable local folder, keeping the hidden `.git` directory.
2. Open GitHub Desktop and select **File → Add local repository…**; choose the extracted `single-galaxy` folder.
3. Select **Publish repository**, name it `single-galaxy`, choose the linked `vjmichael` account, **keep it private**, and publish.
4. Verify that `README.md` and `docs/design/Galactic_Reputation_and_Social_Memory_v0.1.md` are visible in the new repository.

## If an empty GitHub repository is already created

Add its URL as the Git remote and push the existing `main` branch. Never overwrite another repository's contents to force a match.

## Optional command-line alternative

```sh
git remote add origin https://github.com/vjmichael/single-galaxy.git
git push -u origin main
```

First create the empty private repository if it does not exist. The URL above is a *proposed destination*, not a claim that a hosted repository exists.
