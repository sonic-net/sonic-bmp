# How to open a PR to github.com/sonic-net/sonic-bmp (master)

Patch is committed on branch `cisco-sync-pr2-model`. Follow steps below to push and open the PR.

## 1. Fork sonic-net/sonic-bmp (if you have not already)

- Go to https://github.com/sonic-net/sonic-bmp
- Click **Fork** to create your fork (e.g. `https://github.com/YOUR_USERNAME/sonic-bmp`)

## 2. Add your fork and push the branch

```bash
cd /nobackup/fkong/sonic-bmp-upstream
git remote add myfork https://github.com/YOUR_USERNAME/sonic-bmp.git   # or git@github.com:YOUR_USERNAME/sonic-bmp.git
git push -u myfork cisco-sync-pr2-model
```

Replace `YOUR_USERNAME` with your GitHub username.

## 3. Open the Pull Request

1. Go to: **https://github.com/sonic-net/sonic-bmp/compare**
2. Set **base repository** to `sonic-net/sonic-bmp`, **base** to `master`
3. Set **head repository** to your fork, **compare** to `cisco-sync-pr2-model`
4. Click **Create pull request**
5. Paste the contents of `PR_BODY.md` into the PR description (already filled)

Direct link (after pushing):
**https://github.com/sonic-net/sonic-bmp/compare/master...YOUR_USERNAME:sonic-bmp:cisco-sync-pr2-model**

## Branch and base

| Item   | Value |
|--------|--------|
| Your branch | `cisco-sync-pr2-model` |
| Base repo   | `sonic-net/sonic-bmp` |
| Base branch | `master` |
