#![no_std]

use aidoku::{
    error::Result,
    prelude::*,
    std::{String, Vec, net::Request},
    Manga, MangaContentRating, MangaStatus, MangaViewer, Chapter, Page, Filter, FilterType,
};

const BASE_URL: &str = "https://manga18fx.com";
const USER_AGENT: &str = "Mozilla/5.0 (iPhone; CPU iPhone OS 16_0 like Mac OS X) AppleWebKit/605.1.15";

#[no_mangle]
#[export_name = "get_manga_list"]
pub extern "C" fn get_manga_list(filters: Vec<Filter>, page: i32) -> Result<Vec<Manga>> {
    let mut url = String::new();
    let mut is_search = false;
    
    for filter in filters {
        match filter.kind {
            FilterType::Title => {
                let title = filter.value.as_string().read();
                if !title.is_empty() {
                    url = format!("{}/?s={}&post_type=wp-manga", BASE_URL, url_encode(&title));
                    is_search = true;
                }
            }
            FilterType::Genre => {
                let genre = filter.value.as_string().read();
                if !genre.is_empty() && !is_search {
                    url = format!("{}/manga-genre/{}/page/{}/", BASE_URL, genre, page);
                }
            }
            _ => {}
        }
    }
    
    if url.is_empty() {
        url = format!("{}/page/{}/", BASE_URL, page);
    }
    
    let html = Request::get(&url).header("User-Agent", USER_AGENT).html()?;
    let mut manga_list = Vec::new();
    
    for item in html.select(".page-item-detail").array() {
        let node = item.as_node()?;
        let link = node.select("a").attr("href").read();
        let id = link.trim_end_matches('/').split('/').last().unwrap_or("").to_string();
        if id.is_empty() { continue; }
        
        let title = node.select(".post-title a").text().read();
        let cover = node.select("img").attr("data-src").read();
        let cover = if cover.is_empty() { node.select("img").attr("src").read() } else { cover };
        
        manga_list.push(Manga {
            id, cover, title,
            author: String::new(),
            artist: String::new(),
            description: String::new(),
            url: link,
            categories: Vec::new(),
            status: MangaStatus::Unknown,
            nsfw: MangaContentRating::Nsfw,
            viewer: MangaViewer::Rtl,
        });
    }
    
    Ok(manga_list)
}

#[no_mangle]
#[export_name = "get_manga_details"]
pub extern "C" fn get_manga_details(id: String) -> Result<Manga> {
    let url = format!("{}/manga/{}/", BASE_URL, id);
    let html = Request::get(&url).header("User-Agent", USER_AGENT).html()?;
    
    let title = html.select(".post-title h1").text().read();
    let cover = html.select(".summary_image img").attr("data-src").read();
    let cover = if cover.is_empty() { html.select(".summary_image img").attr("src").read() } else { cover };
    let author = html.select(".author-content a").text().read();
    let artist = html.select(".artist-content a").text().read();
    let description = html.select(".description-summary .summary__content").text().read();
    
    let status_text = html.select(".post-status .summary-content").text().read().to_lowercase();
    let status = if status_text.contains("ongoing") { MangaStatus::Ongoing }
        else if status_text.contains("completed") { MangaStatus::Completed }
        else if status_text.contains("hiatus") { MangaStatus::Hiatus }
        else { MangaStatus::Unknown };
    
    let mut categories = Vec::new();
    for cat in html.select(".genres-content a").array() {
        if let Ok(node) = cat.as_node() { categories.push(node.text().read()); }
    }
    
    let nsfw = if categories.iter().any(|c| {
        let c = c.to_lowercase();
        c.contains("adult") || c.contains("mature") || c.contains("ecchi") || c.contains("smut") || c.contains("hentai")
    }) { MangaContentRating::Nsfw } else { MangaContentRating::Safe };
    
    Ok(Manga { id: id.clone(), cover, title, author, artist, description, url, categories, status, nsfw, viewer: MangaViewer::Rtl })
}

#[no_mangle]
#[export_name = "get_chapter_list"]
pub extern "C" fn get_chapter_list(id: String) -> Result<Vec<Chapter>> {
    let url = format!("{}/manga/{}/", BASE_URL, id);
    let html = Request::get(&url).header("User-Agent", USER_AGENT).html()?;
    let mut chapters = Vec::new();
    
    for item in html.select(".wp-manga-chapter").array() {
        let node = item.as_node()?;
        let chapter_url = node.select("a").attr("href").read();
        let chapter_title = node.select("a").text().read().trim().to_string();
        if chapter_url.is_empty() || chapter_title.is_empty() { continue; }
        
        let chapter_num = extract_chapter_num(&chapter_title);
        let date_text = node.select(".chapter-release-date").text().read();
        let date_updated = parse_date(&date_text);
        
        chapters.push(Chapter {
            id: chapter_url.replace(BASE_URL, ""),
            title: chapter_title,
            volume: -1.0,
            chapter: chapter_num,
            date_updated,
            scanlator: String::new(),
            url: chapter_url,
            lang: String::from("en"),
        });
    }
    
    Ok(chapters)
}

#[no_mangle]
#[export_name = "get_page_list"]
pub extern "C" fn get_page_list(_manga_id: String, chapter_id: String) -> Result<Vec<Page>> {
    let url = if chapter_id.starts_with("http") { chapter_id } else { format!("{}{}", BASE_URL, chapter_id) };
    let html = Request::get(&url).header("User-Agent", USER_AGENT).html()?;
    let mut pages = Vec::new();
    
    for (idx, img) in html.select(".reading-content img").array().enumerate() {
        let node = img.as_node()?;
        let mut img_url = node.attr("data-src").read();
        if img_url.is_empty() { img_url = node.attr("src").read(); }
        if img_url.is_empty() || img_url.contains("blank") { continue; }
        
        pages.push(Page {
            index: idx as i32,
            url: img_url.trim().to_string(),
            base64: String::new(),
            text: String::new(),
        });
    }
    
    Ok(pages)
}

#[no_mangle]
#[export_name = "modify_image_request"]
pub extern "C" fn modify_image_request(request: Request) {
    request.header("Referer", BASE_URL).header("User-Agent", USER_AGENT);
}

fn url_encode(s: &str) -> String {
    let mut result = String::new();
    for byte in s.bytes() {
        match byte {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => result.push(byte as char),
            b' ' => result.push('+'),
            _ => { result.push('%'); result.push_str(&format!("{:02X}", byte)); }
        }
    }
    result
}

fn extract_chapter_num(title: &str) -> f32 {
    let lower = title.to_lowercase();
    for pattern in ["chapter", "ch", "ep", "episode"] {
        if let Some(pos) = lower.find(pattern) {
            let after = &lower[pos + pattern.len()..];
            let num_part: String = after.chars().skip_while(|c| !c.is_ascii_digit()).take_while(|c| c.is_ascii_digit() || *c == '.').collect();
            if let Ok(num) = num_part.parse::<f32>() { return num; }
        }
    }
    let nums: String = title.chars().filter(|c| c.is_ascii_digit() || *c == '.').collect();
    nums.parse::<f32>().unwrap_or(0.0)
}

fn parse_date(date_str: &str) -> i64 {
    let lower = date_str.to_lowercase();
    if lower.contains("min") || lower.contains("hour") || lower.contains("just now") { return 0; }
    if lower.contains("day") { return -86400; }
    if lower.contains("week") { return -604800; }
    if lower.contains("month") { return -2592000; }
    if lower.contains("year") { return -31536000; }
    -1
}
