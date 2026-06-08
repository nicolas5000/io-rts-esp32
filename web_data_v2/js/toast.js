(function () {
    var container = document.createElement("div");
    container.id = "toast-container";
    document.body.appendChild(container);

    function dismissToast(toast) {
        if (!toast || !toast.parentNode) return;
        toast.classList.add("toast-hide");
        setTimeout(function () {
            if (toast.parentNode) toast.parentNode.removeChild(toast);
        }, 300);
    }

    window.showToast = function (msg, type, duration) {
        var toast = document.createElement("div");
        toast.className = "toast" + (type ? " toast-" + type : "");
        toast.textContent = msg;
        container.appendChild(toast);
        var ms = (duration || 3000);
        var timer = setTimeout(function () { dismissToast(toast); }, ms);
        toast._dismiss = function () { clearTimeout(timer); dismissToast(toast); };
        return toast;
    };
})();
