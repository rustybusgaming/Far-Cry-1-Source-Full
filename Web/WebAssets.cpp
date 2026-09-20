////////////////////////////////////////////////////////////////////////////
//
//  CryEngine web port
// -------------------------------------------------------------------------
//  File name:   WebAssets.cpp
//  Description: The browser side of pointing the engine at a game install.
//               See WebAssets.h for why it works this way.
//
////////////////////////////////////////////////////////////////////////////

#include "WebAssets.h"

#if defined(__EMSCRIPTEN__)

#include <emscripten.h>

#include <stdio.h>
#include <string.h>

//! Where picked files are written. A directory rather than the root so that
//! nothing the user supplies can collide with the build's own files.
static const char* kAssetRoot = "/gamedata";

//! The budget, in bytes. MEMFS is memory, and the page gets a fixed heap.
//!
//! 768 MB is chosen to be comfortably larger than scripts, fonts and
//! configuration -- the part that gets the engine past script loading -- and
//! comfortably smaller than a full installation, so that someone selecting
//! their whole Far Cry folder is refused with an explanation instead of
//! crashing on an allocation somewhere unrelated.
static const double kBudgetBytes = 768.0 * 1024.0 * 1024.0;

static int    g_nState = eWebAssets_Waiting;
static double g_nBytes = 0.0;

//////////////////////////////////////////////////////////////////////////
// Called from JavaScript once the user has decided.
//////////////////////////////////////////////////////////////////////////

extern "C"
{

EMSCRIPTEN_KEEPALIVE void WebAssets_OnReady(double nBytes)
{
	g_nBytes = nBytes;
	g_nState = eWebAssets_Ready;
	printf("[assets] %.1f MB written to %s\n",
	       nBytes / (1024.0 * 1024.0), kAssetRoot);
}

EMSCRIPTEN_KEEPALIVE void WebAssets_OnSkipped()
{
	g_nState = eWebAssets_Skipped;
	printf("[assets] continuing without game data\n");
}

}

//////////////////////////////////////////////////////////////////////////
// The picker.
//
// EM_ASM rather than a file in the shell: the shell is Emscripten's own and
// replacing it to add a dialog would mean owning a copy of it forever.
//
// No commas at brace depth zero inside the block -- EM_ASM is a macro and the
// preprocessor splits its arguments on them. Every statement below ends in a
// semicolon for that reason.
//////////////////////////////////////////////////////////////////////////
void WebAssets_Begin()
{
	EM_ASM({
		var sRoot = UTF8ToString($0);
		var nBudget = $1;

		try { FS.mkdir(sRoot); } catch (e) {}

		// "?nodata" skips the dialog entirely.
		//
		// Not a debug hatch: it is how anything unattended runs. The browser
		// tests use it, and so does anyone opening the build just to see the
		// engine come up -- without it the page would sit behind a dialog
		// waiting for a folder that a CI runner does not have.
		// A plain substring test, not a regular expression. The first version
		// used one, and its backslashes did not survive being stringified by
		// the EM_ASM macro -- the page threw "Invalid regular expression"
		// before the engine ever started. Escaping inside EM_ASM is not worth
		// the risk when indexOf does the job.
		if (window.location.search.indexOf('nodata') >= 0) {
			setTimeout(function () { Module._WebAssets_OnSkipped(); }, 0);
			return;
		}

		var panel = document.createElement('div');
		panel.style.cssText =
			'position:fixed; inset:0; z-index:99999; display:flex;' +
			'align-items:center; justify-content:center;' +
			'background:rgba(0,0,0,0.88); color:#ddd;' +
			'font:14px/1.5 system-ui, sans-serif;';

		var card = document.createElement('div');
		card.style.cssText =
			'max-width:620px; padding:28px 32px; background:#141414;' +
			'border:1px solid #333; border-radius:8px;';

		card.innerHTML =
			'<h2 style="margin:0 0 12px; font-size:18px; color:#fff">' +
			'Far Cry data</h2>' +
			'<p style="margin:0 0 12px">This port has no game data of its ' +
			'own, and cannot: Far Cry&rsquo;s assets are several GB and are ' +
			'not redistributable.</p>' +
			'<p style="margin:0 0 12px">Choose your own Far Cry folder &mdash; ' +
			'the one containing <code>FCData</code>. The files are read in ' +
			'this tab and written into the page&rsquo;s own in-memory ' +
			'filesystem. <strong>Nothing is uploaded</strong> and nothing ' +
			'leaves your machine.</p>' +
			'<p style="margin:0 0 16px; color:#999">That filesystem is RAM, ' +
			'so there is a ' + Math.round(nBudget / 1048576) + '&nbsp;MB ' +
			'limit. Scripts, fonts and configuration fit comfortably; a ' +
			'whole installation will not, and selecting one is refused ' +
			'rather than left to fail later.</p>' +
			'<div id="cry-assets-status" style="margin:0 0 16px; ' +
			'color:#8ab4f8; min-height:1.5em"></div>';

		var pick = document.createElement('input');
		pick.type = 'file';
		pick.webkitdirectory = true;
		pick.multiple = true;
		pick.style.cssText = 'display:block; margin-bottom:14px; color:#ddd';

		var skip = document.createElement('button');
		skip.textContent = 'Continue without game data';
		skip.style.cssText =
			'padding:8px 14px; background:#222; color:#ddd;' +
			'border:1px solid #444; border-radius:4px; cursor:pointer';

		card.appendChild(pick);
		card.appendChild(skip);
		panel.appendChild(card);
		document.body.appendChild(panel);

		var status = card.querySelector('#cry-assets-status');

		function finishSkipped() {
			panel.remove();
			Module._WebAssets_OnSkipped();
		}

		skip.onclick = finishSkipped;

		pick.onchange = function () {
			var files = Array.prototype.slice.call(pick.files || []);
			if (!files.length) { return; }

			var nTotal = 0;
			for (var i = 0; i < files.length; i++) { nTotal += files[i].size; }

			if (nTotal > nBudget) {
				status.style.color = '#f28b82';
				status.textContent =
					'That folder is ' + (nTotal / 1048576).toFixed(0) +
					' MB, over the ' + Math.round(nBudget / 1048576) +
					' MB limit. Select a subfolder instead -- the scripts ' +
					'and fonts are what matter first.';
				pick.value = '';
				return;
			}

			status.style.color = '#8ab4f8';
			status.textContent =
				'Reading ' + files.length + ' files (' +
				(nTotal / 1048576).toFixed(0) + ' MB)...';

			// Sequential rather than parallel: every file becomes a MEMFS
			// allocation, and starting thousands of reads at once peaks at
			// far more memory than the total the budget just approved.
			var nIndex = 0;
			var nWritten = 0;

			function mkdirp(sPath) {
				var parts = sPath.split('/');
				var sCur = '';
				for (var i = 0; i < parts.length; i++) {
					if (!parts[i]) { continue; }
					sCur += '/' + parts[i];
					try { FS.mkdir(sCur); } catch (e) {}
				}
			}

			function next() {
				if (nIndex >= files.length) {
					panel.remove();
					Module._WebAssets_OnReady(nWritten);
					return;
				}

				var file = files[nIndex++];

				// webkitRelativePath keeps the folder structure the user
				// picked, including the top-level folder name. Dropping that
				// first component is what makes "the folder containing
				// FCData" land as FCData/... rather than FarCry/FCData/...
				var sRel = file.webkitRelativePath || file.name;
				var nSlash = sRel.indexOf('/');
				if (nSlash >= 0) { sRel = sRel.substring(nSlash + 1); }
				if (!sRel) { next(); return; }

				var sDest = sRoot + '/' + sRel;
				var nDir = sDest.lastIndexOf('/');
				if (nDir > 0) { mkdirp(sDest.substring(0, nDir)); }

				var reader = new FileReader();
				reader.onload = function () {
					try {
						FS.writeFile(sDest, new Uint8Array(reader.result));
						nWritten += file.size;
					} catch (e) {
						console.error('[assets] could not write ' + sDest, e);
					}

					if ((nIndex % 64) === 0 || nIndex === files.length) {
						status.textContent =
							'Reading... ' + nIndex + ' / ' + files.length;
					}
					next();
				};
				reader.onerror = function () {
					console.error('[assets] could not read ' + sRel);
					next();
				};
				reader.readAsArrayBuffer(file);
			}

			next();
		};
	}, kAssetRoot, kBudgetBytes);
}

//////////////////////////////////////////////////////////////////////////

EWebAssetsState WebAssets_State()
{
	return (EWebAssetsState)g_nState;
}

const char* WebAssets_Root()
{
	return g_nState == eWebAssets_Ready ? kAssetRoot : "";
}

double WebAssets_BytesWritten()
{
	return g_nBytes;
}

#endif //__EMSCRIPTEN__
