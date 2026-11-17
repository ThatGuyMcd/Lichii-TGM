#!/usr/bin/env python3
"""Plan one patch release per successful London night; no GitHub mutations."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
from zoneinfo import ZoneInfo

TAG = re.compile(r'^Lichii-tgm-v(\d+)\.(\d+)(?:\.(\d+))?$')

def plan(releases, journal, has_changes, now=None):
    now = now or datetime.now(timezone.utc)
    if now.tzinfo is None:
        raise ValueError('A timezone-aware date is required')
    date = now.astimezone(ZoneInfo('Europe/London')).date().isoformat()
    if any(r.get('london_date') == date and r.get('status') == 'published'
           for r in journal.get('runs', [])):
        return {'action': 'already_published', 'london_date': date}
    for release in releases:
        published_at = release.get('published_at')
        if (TAG.fullmatch(release.get('tag_name', '')) and not release.get('draft', False)
                and not release.get('prerelease', False) and published_at):
            published = datetime.fromisoformat(published_at.replace('Z', '+00:00'))
            if published.astimezone(ZoneInfo('Europe/London')).date().isoformat() == date:
                return {'action': 'already_published', 'london_date': date,
                        'tag': release['tag_name']}
    if not has_changes:
        return {'action': 'no_changes', 'london_date': date}
    published = []
    for release in releases:
        match = TAG.fullmatch(release.get('tag_name', ''))
        if match and not release.get('draft', False) and not release.get('prerelease', False):
            published.append((tuple(int(n or 0) for n in match.groups()), release))
    if not published:
        raise ValueError('No published fork version establishes the next patch version')
    version, previous = max(published, key=lambda item: item[0])
    next_version = f'{version[0]}.{version[1]}.{version[2] + 1}'
    tag = 'Lichii-tgm-v' + next_version
    drafts = [r for r in releases if r.get('draft') and r.get('tag_name') == tag]
    if len(drafts) > 1:
        raise ValueError('Conflicting release drafts')
    return {'action': 'resume_draft' if drafts else 'prepare', 'london_date': date,
            'previous_release': previous['tag_name'], 'version': next_version, 'tag': tag,
            'draft_id': drafts[0].get('id') if drafts else None,
            'preserve_existing_tags': True, 'requires_exact_source_validation': True}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--releases', type=Path, required=True)
    parser.add_argument('--journal', type=Path, required=True)
    parser.add_argument('--has-changes', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    journal = json.loads(args.journal.read_text()) if args.journal.exists() else {'runs': []}
    result = plan(json.loads(args.releases.read_text()), journal, args.has_changes)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
