# How to open a PR to github.com/sonic-net/sonic-bmp (master)

## 1. Apply your changes (from Cisco whitebox/sonic-bmp PR #2)

Copy the code changes from:
**https://wwwin-github.cisco.com/whitebox/sonic-bmp/pull/2**

Into this clone:
```bash
cd /nobackup/fkong/sonic-bmp-upstream
# Apply patch, or copy changed files, then:
git add -A
git status
git commit -m "Your commit message (match or summarize Cisco PR #2)"
```

## 2. Fork sonic-net/sonic-bmp (if you have not already)

- Go to https://github.com/sonic-net/sonic-bmp
- Click **Fork** to create your fork (e.g. `https://github.com/YOUR_USERNAME/sonic-bmp`)

## 3. Add your fork and push the branch

```bash
cd /nobackup/fkong/sonic-bmp-upstream
git remote add myfork https://github.com/YOUR_USERNAME/sonic-bmp.git   # or git@github.com:YOUR_USERNAME/sonic-bmp.git
git push -u myfork cisco-sync-pr2-model
```

Replace `YOUR_USERNAME` with your GitHub username.

## 4. Open the Pull Request

1. Go to: **https://github.com/sonic-net/sonic-bmp/compare**
2. Set **base repository** to `sonic-net/sonic-bmp`, **base** to `master`
3. Set **head repository** to your fork, **compare** to `cisco-sync-pr2-model`
4. Click **Create pull request**
5. Paste the contents of `PR_BODY.md` (after filling it from Cisco PR #2) into the PR description

Direct link (after pushing):
**https://github.com/sonic-net/sonic-bmp/compare/master...YOUR_USERNAME:sonic-bmp:cisco-sync-pr2-model**

## Branch and base

| Item   | Value |
|--------|--------|
| Your branch | `cisco-sync-pr2-model` |
| Base repo   | `sonic-net/sonic-bmp` |
| Base branch | `master` |
