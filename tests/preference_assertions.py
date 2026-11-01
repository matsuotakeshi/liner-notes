def assert_preferences_only(before, after):
    """Viewing may autosave preferences, but must not rewrite existing records."""
    assert after.startswith(before)
    tail=after[len(before):]
    assert len(tail)%128==0
    assert all(tail[i]==ord('f') for i in range(0,len(tail),128))
