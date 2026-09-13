import os
import re
import urllib.parse
import urllib.request
import json
import glob
import sys

def translate_text(text, target_lang="tr"):
    if not text.strip():
        return ""
    
    url = f"https://translate.googleapis.com/translate_a/single?client=gtx&sl=auto&tl={target_lang}&dt=t&q=" + urllib.parse.quote(text)
    try:
        req = urllib.request.Request(
            url, 
            headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'}
        )
        with urllib.request.urlopen(req, timeout=4) as response:
            data = json.loads(response.read().decode('utf-8'))
            if data and data[0]:
                res = "".join([part[0] for part in data[0] if part and part[0]])
                return res.strip()
    except Exception:
        pass
    return ""

def get_synced_lyrics(query):
    url = "https://lrclib.net/api/search?q=" + urllib.parse.quote(query)
    req = urllib.request.Request(url, headers={'User-Agent': 'AuraMusic/8.0'})
    try:
        with urllib.request.urlopen(req, timeout=6) as resp:
            results = json.loads(resp.read().decode('utf-8'))
            for item in results:
                if item.get("syncedLyrics"):
                    return item.get("trackName"), item.get("artistName"), item.get("syncedLyrics")
    except Exception:
        pass
    return None, None, None

def main():
    target_lang = "tr"
    
    # lang.cfg formatı: ARAYUZ_DILI HEDEF_CEVIRI_DILI (Örn: en tr)
    if os.path.exists("lang.cfg"):
        try:
            with open("lang.cfg", "r", encoding="utf-8") as f:
                parts = f.read().strip().split()
                if len(parts) >= 2:
                    target_lang = parts[1] # İkinci parametre hedef çeviri dilidir
                elif len(parts) == 1:
                    target_lang = parts[0]
        except Exception:
            pass

    if len(sys.argv) > 1 and len(sys.argv[1].strip()) <= 5:
        target_lang = sys.argv[1].strip()

    target_song = ""
    if len(sys.argv) > 2:
        target_song = " ".join(sys.argv[2:]).strip()
    else:
        mp3_files = glob.glob("music/*.mp3")
        if mp3_files:
            target_song = os.path.splitext(os.path.basename(mp3_files[0]))[0]

    if not target_song:
        return

    query_name = re.sub(r"[_\-\.\(\)]", " ", target_song).strip()
    track, artist, synced_lrc = get_synced_lyrics(query_name)

    if not synced_lrc:
        track = target_song
        artist = "Yerel Parça"
        synced_lrc = "[00:01.00] Sözler bulunamadı (Instrumental)"

    output_lines = [f"[ti:{track}]\n", f"[ar:{artist}]\n"]
    for line in synced_lrc.strip().split("\n"):
        match = re.match(r"^(\[\d{2}:\d{2}\.\d{2,3}\])(.*)", line)
        if match:
            timestamp = match.group(1)
            eng = match.group(2).strip()
            if eng:
                trans = translate_text(eng, target_lang)
                if trans:
                    output_lines.append(f"{timestamp} {eng} ({trans})\n")
                else:
                    output_lines.append(f"{timestamp} {eng}\n")
            else:
                output_lines.append(f"{timestamp} ♪\n")
        else:
            output_lines.append(line + "\n")

    with open("lyrics.lrc", "w", encoding="utf-8") as f:
        f.writelines(output_lines)

if __name__ == "__main__":
    main()