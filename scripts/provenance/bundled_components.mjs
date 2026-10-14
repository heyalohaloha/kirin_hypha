// Versions and original notices from the adopted JUCE tree; retain bytes without rewriting.
export const JUCE_COMMIT = '4f43011b96eb0636104cb3e433894cda98243626';
export const RUST_RUNTIME_VERSION = '1.94.1';
const modules = 'juce_shell/JUCE/modules/';
const row = (id, license, directory, names) => ({
  id, license, source: `https://github.com/juce-framework/JUCE/tree/${JUCE_COMMIT}/modules/${directory}`,
  modificationNotice: 'JUCE-bundled source and upstream JUCE integration changes are retained in Corresponding Source.',
  licenseFiles: names.map(name => modules + directory + '/' + name),
});
export const BUNDLED_COMPONENTS = [
  row('FLAC@1.4.3', 'BSD-3-Clause', 'juce_audio_formats/codecs/flac', ['Flac Licence.txt']),
  row('Ogg@JUCE-7.0.12', 'BSD-3-Clause', 'juce_audio_formats/codecs/oggvorbis', ['Ogg Vorbis Licence.txt']),
  row('Vorbis@1.3.7', 'BSD-3-Clause', 'juce_audio_formats/codecs/oggvorbis', ['Ogg Vorbis Licence.txt', 'libvorbis-1.3.7/COPYING']),
  row('IJG-libjpeg@6b', 'IJG', 'juce_graphics/image_formats/jpglib', ['README']),
  row('libpng@1.6.37', 'Libpng-2.0', 'juce_graphics/image_formats/pnglib', ['LICENSE']),
  row('zlib@1.2.3', 'Zlib', 'juce_core/zip/zlib', ['zlib.h', 'README']),
  row('Apple-AudioUnitSDK@JUCE-7.0.12', 'Apache-2.0', 'juce_audio_plugin_client/AU/AudioUnitSDK', ['LICENSE.txt']),
  row('VST3-SDK@3.7.8', 'GPL-3.0 AND BSD-3-Clause', 'juce_audio_processors/format_types/VST3_SDK',
    ['LICENSE.txt', 'base/LICENSE.txt', 'pluginterfaces/LICENSE.txt', 'public.sdk/LICENSE.txt']),
  { id: `Rust-runtime@${RUST_RUNTIME_VERSION}`, license: 'MIT OR Apache-2.0; additional notices in COPYRIGHT-library.html',
    source: 'https://github.com/rust-lang/rust/tree/e408947bfd200af42db322daf0fadfe7e26d3bd1/library',
    modificationNotice: 'Unmodified standard runtime; upstream standard-library dependency notices retained.',
    licenseFiles: ['LICENSE-MIT', 'LICENSE-APACHE', 'COPYRIGHT-library.html']
      .map(name => `THIRD_PARTY_LICENSES/rust-runtime-${RUST_RUNTIME_VERSION}/${name}`) },
];

export const REQUIRED_COMPONENTS = [
  { id: 'MoSQITo@1.2.1', license: 'Apache-2.0' }, { id: 'JUCE@7.0.12', license: null },
  ...BUNDLED_COMPONENTS,
];

// CMake links juce_audio_formats only into POST. Both roles link graphics/core.
// Keep native-evidence identities with the NOTICE inventory above.
export const NATIVE_LINK_COMPONENTS = [
  { id: 'FLAC@1.4.3', component: 'FLAC', roles: ['POST'], pattern: /FLAC__/ },
  { id: 'Ogg@JUCE-7.0.12', component: 'Ogg', roles: ['POST'], pattern: /ogg_(?:stream|sync|page|packet)_/ },
  { id: 'Vorbis@1.3.7', component: 'Vorbis', roles: ['POST'], pattern: /vorbis_/ },
  { id: 'IJG-libjpeg@6b', component: 'IJG', roles: ['PRE', 'POST'], pattern: /jpeg_/ },
  { id: 'libpng@1.6.37', component: 'PNG', roles: ['PRE', 'POST'], pattern: /png_/ },
  { id: 'zlib@1.2.3', component: 'zlib', roles: ['PRE', 'POST'], pattern: /(?:inflate|deflate)(?:End|Init|Reset|@)/ },
];
