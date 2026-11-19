# Lichii-tgm-v1.0.7

I've brought across an inline-index buffer fix from
[Lichii PR #2163](https://github.com/zortos293/Lichii/pull/2163),
[commit e10fe57ba2](https://github.com/zortos293/Lichii/commit/e10fe57ba239eccf63393765b96b13105ace6553),
by Dimitris Zervas and the Lichii contributors.

I've fixed the buffer's lifetime when it is collected or needs to grow. The next
draw creates the right buffer, and the old allocation leaves the cache's accounting
and stays alive through the existing retirement path. This prevents a freed or
unrelated buffer slot being used for inline indices and keeps memory accounting
and collection in step.

I've kept our Forest Lantern and Couch Mode UI, every setting, controller
navigation, saves, data paths, OOT patches and aspect-ratio options intact.
Frame generation remains off. Windows and Linux AppImage downloads are included.

I've retained the OOT shader preparation profile with redistribution permission.
Shader precompile recipe by Frizti (Fritchii). Its copyright notice is included
alongside the recipe. Shader code is assembled locally from your own game files;
games, keys and firmware are not included.

Thanks to the Lichii, Fritchii, Eglantine, Eden and yuzu contributors. Their original
copyright notices and licences remain.
