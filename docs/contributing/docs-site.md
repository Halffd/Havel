# Documentation site (GitHub Pages)

The documentation site is mkdocs-material, deployed by
`.github/workflows/docs.yml` to https://halffd.github.io/Havel/.

## How deployment works

- **Trigger**: pushes that touch `docs/**`, `mkdocs.yml`, or the workflow
  file itself, plus manual `workflow_dispatch`.
- **Build**: `mkdocs build --strict` with the Python deps from
  `docs/requirements.txt` (mkdocs-material, pymdown-extensions,
  mkdocs-minify-plugin).
- **Deploy**: `actions/upload-pages-artifact` + `actions/deploy-pages`.

## One-time repository setting

Repo **Settings → Pages → Build and deployment → Source: GitHub Actions**.
Until this is set, the workflow builds and deploys into the Pages
environment but nothing serves (the page 404s). The Pages URL uses the
repository's exact case: `halffd.github.io/Havel`, not `halfd.github.io/Havel`.

## What `--strict` enforces (and what it has caught)

A strict build fails on warnings, not just errors. Every failure below was
a real defect the first Pages run surfaced:

| Failure | Cause | Fix |
|---|---|---|
| `No file matched to **/requirements.txt` | `actions/setup-python` with `cache: pip` needs a dependency file to hash | `docs/requirements.txt` + `cache-dependency-path` |
| `... has no git logs` (git-revision-date-localized) | files whose only commit is a merge: `git log -- <path>` hides merge commits | plugin removed from the config (the date stamps were not worth a gate that cannot pass); keep `fetch-depth: 0` only if a date plugin returns |
| `Doc file '...' contains a link '...', but the target ... is not found` | dead nav entries or dead in-page links | fix the link, remove the nav entry, or exclude scratch directories via `exclude_docs` (used for `docs/plans/`) |

Keep the nav honest: a nav entry for a page that does not exist fails the
build. That is intentional - the site cannot silently lose a page.

## Conventions

- `site_url` must match the served URL (`https://halffd.github.io/Havel/`)
  or canonical links and the sitemap point elsewhere. Switch it if a
  custom domain is configured.
- New modules/pages: add the doc under `docs/`, then wire it into `nav` in
  `mkdocs.yml` in the same commit; the strict build fails the push
  otherwise.
- Working notes and design plans stay in `docs/plans/` (excluded from the
  build).
