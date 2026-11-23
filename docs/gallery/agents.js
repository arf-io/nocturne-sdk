const $ = (selector) => document.querySelector(selector);
const reduced = matchMedia('(prefers-reduced-motion: reduce)');
const stream = $('#stream');
const status = $('#status');
const dialog = $('#diff');
const files = [
    {name: 'src/workspace.c', count: '+2 −1', lines: ['@@ workspace layout @@', '- mount(activity);', '+ mount(split(activity, approvals));', '+ mount(changed_files);']},
    {name: 'src/stream.c', count: '+2 −1', lines: ['@@ apply incoming output @@', '- replace_message(event.text);', '+ append_chunk(event.text);', '+ request_next_frame();']},
    {name: 'tests/workspace.c', count: '+3', lines: ['@@ keyboard navigation @@', '+ select_changed_file(0);', '+ assert_diff_is_open();', '+ close_diff_with_escape();']},
];

for (const file of files) {
    const item = document.createElement('li');
    const button = document.createElement('button');
    const count = document.createElement('span');
    button.textContent = file.name;
    count.textContent = file.count;
    button.append(count);
    button.addEventListener('click', () => {
        $('#diff-title').textContent = file.name;
        $('#patch').replaceChildren(...file.lines.map((line) => {
            const row = document.createElement('span');
            row.textContent = line;
            if (line.startsWith('+')) row.className = 'added';
            if (line.startsWith('-')) row.className = 'removed';
            return row;
        }));
        dialog.showModal();
    });
    item.append(button);
    $('#files').append(item);
}

const initial = [
    ['You', 'Build an agent workspace with live output, reviewable approvals, and file diffs.'],
    ['Orbit · layout', 'The activity stream has its own scroll area. Approvals and changed files stay within reach as output arrives.'],
    ['Vesper · implementation', 'Streaming chunks append to the active message. Selecting a file opens its diff in a keyboard-accessible dialog.'],
    ['Kepler · review', 'The three sample diffs are ready to inspect. Approve or decline the simulated preview run.'],
];
let queue = [];
let busy = false;
let generation = 0;
const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function flush() {
    if (busy) return;
    busy = true;
    const current = generation;
    status.textContent = 'Streaming';
    while (queue.length) {
        const [name, content] = queue.shift();
        const item = document.createElement('article');
        const author = document.createElement('strong');
        const body = document.createElement('p');
        item.className = 'event';
        author.textContent = name;
        item.append(author, body);
        stream.append(item);
        if (reduced.matches || name === 'You') {
            body.textContent = content;
        } else {
            body.classList.add('typing');
            for (let index = 3; index < content.length + 3; index += 3) {
                const follow = stream.scrollHeight - stream.scrollTop - stream.clientHeight < 100;
                body.textContent = content.slice(0, index);
                if (follow) stream.scrollTop = stream.scrollHeight;
                await wait(23);
                if (current !== generation) return;
            }
            body.classList.remove('typing');
        }
        if (stream.scrollHeight - stream.scrollTop - stream.clientHeight < 100) {
            stream.scrollTop = stream.scrollHeight;
        }
    }
    busy = false;
    status.textContent = 'Ready';
}
function enqueue(...events) {
    queue.push(...events);
    void flush();
}
function start() {
    generation++;
    queue = [];
    busy = false;
    stream.replaceChildren();
    $('#approval').textContent = 'Orbit requests a local preview run.';
    document.querySelectorAll('[data-decision]').forEach((button) => { button.disabled = false; });
    enqueue(...initial);
}
$('#replay').addEventListener('click', start);
$('#prompt').addEventListener('submit', (event) => {
    event.preventDefault();
    const input = $('#message');
    const message = input.value.trim();
    if (!message) return;
    input.value = '';
    enqueue(['You', message], ['Kepler · simulated reply', 'Open a changed file to inspect the patch, or replay the session to try the approval flow again.']);
});
document.querySelectorAll('[data-decision]').forEach((button) => {
    button.addEventListener('click', () => {
        const approved = button.dataset.decision === 'approve';
        $('#approval').textContent = approved ? 'Preview approved in this demo.' : 'Preview declined in this demo.';
        document.querySelectorAll('[data-decision]').forEach((choice) => { choice.disabled = true; });
        enqueue(['Orbit · layout', approved ? 'Approval recorded. Preview marked as complete.' : 'Decline recorded. Preview skipped.']);
    });
});

const canvas = $('#stars');
const ctx = canvas.getContext('2d');
const stars = Array.from({length: 240}, (_, index) => ({
    x: ((index * 71.313) % 241) / 241,
    y: ((index * index * 11.793) % 233) / 233,
    depth: (index % 13 + 1) / 13,
}));
function paint(time) {
    if (canvas.width !== innerWidth || canvas.height !== innerHeight) {
        canvas.width = innerWidth;
        canvas.height = innerHeight;
    }
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    for (const star of stars) {
        const drift = reduced.matches ? 0 : time * star.depth * 0.013;
        const x = (star.x * canvas.width + drift) % canvas.width;
        const y = (star.y * canvas.height + canvas.width * star.x * .36 + drift * .36) % canvas.height;
        ctx.strokeStyle = `rgba(184,172,255,${0.17 + star.depth * .52})`;
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x + star.depth * 5 + 1, y + star.depth * 2);
        ctx.stroke();
    }
}
let frame;
function animate(time) {
    paint(time);
    if (!reduced.matches) frame = requestAnimationFrame(animate);
}
function restartSky() {
    cancelAnimationFrame(frame);
    animate(0);
}
reduced.addEventListener('change', restartSky);
addEventListener('resize', () => { if (reduced.matches) paint(0); });
start();
restartSky();
