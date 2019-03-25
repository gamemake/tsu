# ![TypeScript for Unreal][bnr]

A scripting plugin for Unreal Engine 4 that lets you augment Blueprint with functions written in [TypeScript][tsc].

Want to try it out? Check out the [examples project][tex].

Need help with something? Join us on [Discord][dsc].

## Table of Contents

- [Features](#features)
- [Downloads](#downloads)
- [Setup](#setup)
- [Motivation](#motivation)
- [Building](#building)
- [License](#license)

## Features

### Debugging

Debugging is supported through the V8 inspector protocol, which means you can debug your TypeScript code like you normally would in any editor that supports it, e.g. [Visual Studio Code][vsc] or [WebStorm][wbs].

![Example][exd]

### Hot-reloading

TSU utilizes the existing auto-reimport features in UE4, meaning your code will hot-reload as soon as your file is saved, even while you're running the game, giving you a near real-time development experience.

![Example][exh]

### Blueprint integration

Functions written in TypeScript look and behave just like regular Blueprint functions.

![Example][exb]

### Typings

Type declarations (or "typings") are generated on-the-fly to make sure everything in Blueprint is available from TypeScript.

```ts
// Intermediate/Typings/BP_Cube/index.d.ts

import { Actor } from 'UE/Actor';
import { Class } from 'UE/Class';
import { StaticMeshComponent } from 'UE/StaticMeshComponent';
import { UObject } from 'UE/UObject';

declare class BP_Cube extends Actor {
    cube: StaticMeshComponent;
    static readonly staticClass: Class;
    public constructor(outer?: UObject);
}

export { BP_Cube }
```

This also applies to any third-party Blueprint API, meaning you could install something like [the Socket.IO plugin][sio] and start using it from TypeScript right away.

### CommonJS

With support for the CommonJS module format you can make use of existing TypeScript/JavaScript packages (assuming they don't rely on any web or Node.js API's). One such package would be the popular utility library [Lodash][lds].

```ts
import * as _ from 'lodash';

export function spam(message: string) {
    _.times(100, () => console.log(message));
}
```

### Proven technologies

Battle-tested technologies make up the foundation of TSU, such as the V8 JavaScript engine and the official TypeScript compiler, in order to provide a robust and familiar experience for developers.

## Downloads

_(TSU will be up on [Unreal Marketplace][mkt] once it's in beta)._

Please note that **currently**...

- TSU is in its **alpha** stage
- TSU has **only** been tested with **Unreal Engine 4.21**
- You can **not** use TSU outside of Win64 **editor** builds
- You can **not** use TSU in packaged/cooked builds
- There **will** be breaking changes in upcoming versions

Hopefully all of these issues will be resolved soon, but please be aware of them before downloading.

If you still want to try it, head over to [Releases][rls].

## Setup

_(For a reference to these steps you can look at the [examples project][tex])_

### Basics

You'll need a `Content/Scripts/tsconfig.json` that at the very least has these compiler options:

```json
{
  "compilerOptions": {
    "baseUrl": ".",
    "rootDir": ".",
    "sourceRoot": ".",
    "target": "es2016",
    "lib": [ "es2016" ],
    "module": "commonjs",
    "inlineSourceMap": true,
    "noEmitOnError": true,
    "noImplicitReturns": true,
    "pretty": false,
    "skipLibCheck": true,
    "strict": true,
    "paths": { "UE/*": [ "../../Intermediate/Typings/*" ] },
    "typeRoots": [ "../../Intermediate/Typings" ],
    "types": [ "TsuGlobals" ]
  },
  "include": [ "Source/**/*" ]
}
```

You can also add more [compiler options][opt] to it, like `noUnusedLocals` and `noUnusedParameters`.

Now you can start adding your `.ts` files to `Content/Scripts/Source`.

### Hot-reloading

Hot-reloading relies on the auto reimport feature in UE4, which you'll find in `Editor Preferences` under `Loading & Saving`.

Make sure `Monitor Content Directories` is enabled, then expand the advanced settings and make sure your settings look something like this:

![Advanced settings][imp]

`Import Threshold Time` will decide how fast your changes get hot-reloaded. Setting it to something as low as `0.0 s` might have adverse effects for regular content assets, but if you're only ever reimporting TypeScript files you should be fine.

If you decide to add more files to your project, like `package.json` or `tslint.json` you'll want to add those as exclusions as well.

### Debugging (with Visual Studio Code)

First off, add `Scripts/.vscode/*` to your reimport exclusions (as seen above).

TSU listens for V8 debuggers on port 19216. So you'll want a `Content/Scripts/.vscode/launch.json` that looks like this:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "TSU",
      "type": "node",
      "request": "attach",
      "protocol": "inspector",
      "port": 19216,
      "sourceMaps": true,
      "sourceMapPathOverrides": {
        "*": "${workspaceFolder}/*",
      }
    }
  ]
}
```

The most important part here, outside of the port number, is `sourceMaps` and `sourceMapPathOverrides`. Without these you won't be able to put breakpoints in your TypeScript files.

### Node.js packages

If you want to add a package like [Lodash][lds] to your project, you need to take the following steps:

First off, add the following reimport exclusions (as seen above):

- `Scripts/node_modules/*`
- `Scripts/package.json`
- `Scripts/package-lock.json`

Make sure you hade [Node.js][njs] installed.

Then you'll want to add a `package.json` (if you don't have one already). In `Content/Scripts` run:

```sh
npm init -y
```

Next you'll want to install Lodash. In `Content/Scripts` run:

```sh
npm install lodash
npm install -D @types/lodash
```

Lastly you'll want to add support for `@types` in your `tsconfig.json` by adding it to `typeRoots` like so:

```json
"typeRoots": [
    "../../Intermediate/Typings",
    "./node_modules/@types"
],
```

## Motivation

_"Augmenting Blueprint rather than replacing it."_

First off, a big "Thank you!" goes out to [Unreal.js][ujs] and its contributors. The question "How did Unreal.js solve this?" has been uttered more than once during the development of TSU, and having it be available under a liberal open-source license has been a huge boon. It's part of the reason why TSU is open-source as well.

With that said, there was a need for a scripting plugin that keeps things simple and interacts more natively with Blueprint. Something that allows you to write a Blueprint function in a scripting language and have it show up in Blueprint on-the-fly to be used immediately.

This means that you are not able to create new Blueprint classes with TSU, or write UMG/Slate structures in it. By limiting it to only static functions we minimize the surface area on which problems or disparities can arise, and focus solely on augmenting Blueprint rather than replacing it.

The choice of TypeScript came out of wanting a language that had a sufficient enough type system that most things in Blueprint could be expressed in it, together with an official parser API. On top of having exactly that TypeScript also has world-class integration with editors like Visual Studio Code. And since it boils down to JavaScript in the end there's also a selection of embeddable, fast and battle-hardened runtimes, like V8 and ChakraCore.

## Building

_(This only applies if you're not using the pre-built binaries found in [Releases][rls])._

### Prerequisites

- [Unreal Engine 4.21][ue4]
- [Visual Studio 2017][vss]
- (Optional) [Node.js 10.15][njs] (or later)

### Plugin

- Clone the [examples project][tex] (or use your own)
- Make sure `TSU` is in the `Plugins` directory of the above mentioned project
- Build from Visual Studio...
    - Generate project files from the context menu of the `.uproject` file
    - Set configuration to `Development Editor`
    - Set platform to `Win64`
    - Run `Build Solution` from the `Build` menu
- OR build from command-line...
    - `UnrealBuildTool.exe "C:\Path\To\TsuExamples.uproject" TsuExamplesEditor Win64 Development`

### Parser

_(Already bundled, which means this is optional)._

```sh
cd Source/TsuParser
npm install
npm run build
npm run package
```

### Bootstrap

_(Already bundled, which means this is optional)._

```sh
cd Source/TsuBootstrap
npm install
npm run build
```

## License

TypeScript for Unreal is licensed under the 3-clause BSD license. See the [LICENSE][lic] file for details.

[bnr]: https://user-images.githubusercontent.com/4884246/54883366-87e36180-4e65-11e9-8bc9-5fdb6b5cd462.png
[dsc]: https://discord.gg/QPrNpAQ
[exb]: https://user-images.githubusercontent.com/4884246/54877994-5e571580-4e26-11e9-87ee-a4947f916ceb.png
[exd]: https://user-images.githubusercontent.com/4884246/54877939-a6c20380-4e25-11e9-8abe-78e037b0b23b.gif
[exh]: https://user-images.githubusercontent.com/4884246/54877941-b3465c00-4e25-11e9-8ea2-eec26373a444.gif
[imp]: https://user-images.githubusercontent.com/4884246/54918144-c9275000-4efd-11e9-99ab-5355c4eba16a.png
[lds]: https://www.npmjs.com/package/lodash
[lic]: LICENSE.md
[mkt]: https://www.unrealengine.com/marketplace/
[njs]: https://nodejs.org/en/download/
[opt]: https://www.typescriptlang.org/docs/handbook/compiler-options.html
[rls]: https://github.com/mihe/tsu/releases
[sio]: https://github.com/getnamo/socketio-client-ue4
[tex]: https://github.com/mihe/tsu-examples/tree/master
[tsc]: https://www.typescriptlang.org/
[ue4]: https://docs.unrealengine.com/en-US/GettingStarted/Installation
[ujs]: https://github.com/ncsoft/Unreal.js
[vsc]: https://code.visualstudio.com/
[vss]: https://docs.unrealengine.com/en-us/Programming/Development/VisualStudioSetup
[wbs]: https://www.jetbrains.com/webstorm/